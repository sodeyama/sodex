# Plan 04: 平文 HTTP 結合テスト

## 概要

Plan 01–03 の成果物を結合し、QEMU からホスト側モックサーバへ
平文 HTTP POST + JSON の往復ができることを確認する。
DNS/TLS なしの最小経路で全層を通す最初の結合ポイント。

## 目標

- QEMU 内から `http://10.0.2.2:8080/echo` に JSON POST して応答を受け取る
- 応答 JSON をパースしてフィールド値をシリアルに出力する
- 失敗時に TCP/HTTP/JSON のどこで止まったか切り分けできる

## 設計

### ホスト側モックサーバ

Python スクリプトで最小 HTTP サーバを立てる:

```
POST /echo → リクエストボディをそのまま返す (Content-Type: application/json)
GET  /healthz → {"status":"ok"}
POST /mock/claude → Claude API 風の固定レスポンスを返す（SSE なし、JSON 一括）
POST /mock/claude/error → 429 + Retry-After ヘッダ
```

### ゲスト側テストエントリ

`kernel.c` にフラグ付きの bring-up テスト経路を作る:

```c
/* AGENT_BRINGUP_TEST が define されていたら通常起動の代わりにテストを実行 */
void agent_bringup_test(void)
{
    /* 1. TCP 接続テスト */
    com1_printf("TEST: tcp_connect... ");
    /* ... */

    /* 2. HTTP POST テスト */
    com1_printf("TEST: http_post_echo... ");
    /* ... */

    /* 3. JSON パース テスト */
    com1_printf("TEST: json_parse... ");
    /* ... */

    com1_printf("ALL TESTS PASSED\n");
}
```

### 診断出力フォーマット

```
[AGENT-TEST] tcp_connect 10.0.2.2:8080 ... OK (320ms)
[AGENT-TEST] http_post /echo ... HTTP 200, body_len=42
[AGENT-TEST] json_parse ... found key "status" = "ok"
[AGENT-TEST] http_post /mock/claude ... HTTP 200, content[0].type = "text"
[AGENT-TEST] http_post /mock/claude/error ... HTTP 429, retry_after=30
[AGENT-TEST] RESULT: 5/5 passed
```

## 実装ステップ

1. `tests/mock_http_server.py` を作成する（echo, healthz, mock/claude エンドポイント）
2. `src/agent/bringup_test.c` にテストエントリを実装する
3. `kernel.c` に `#ifdef AGENT_BRINGUP_TEST` の分岐を追加する
4. `makefile` に `make test-agent-bringup` ターゲットを追加する
   - モックサーバをバックグラウンド起動
   - QEMU を `-DAGENT_BRINGUP_TEST` 付きでビルド・起動
   - シリアル出力から PASS/FAIL を判定
   - モックサーバを停止
5. 各テストステップの失敗で明確なエラーメッセージを出す
6. 全テスト通過で QEMU が exit する（`outb(0xf4, 0x00)` 等）

## テスト

### 自動化

```bash
make test-agent-bringup
# 1. mock サーバ起動 (localhost:8080)
# 2. QEMU 起動 (hostfwd で guest:8080 → mock)
#    ※ QEMU user-mode networking: 10.0.2.2 = ホスト
# 3. serial.log から "RESULT: N/N passed" を grep
# 4. 終了コードで CI に PASS/FAIL を返す
```

### 手動確認

- モックサーバを手動起動し、QEMU の VGA/シリアル出力を目視確認
- Wireshark で TCP パケットキャプチャ（トラブルシュート時）

## 変更対象

- 新規:
  - `src/agent/bringup_test.c`
  - `src/include/agent/bringup_test.h`
  - `tests/mock_http_server.py`
- 既存:
  - `src/kernel.c` — テスト分岐追加
  - `src/makefile` — テストターゲット追加

## 完了条件

- `make test-agent-bringup` が 1 コマンドで回り、全テスト PASS する
- TCP 接続失敗、HTTP エラー、JSON パース失敗がそれぞれ別のエラーメッセージで出る
- モックの Claude 応答 JSON から `content[0].text` を取り出せる
- DNS/TLS を使っていない（固定 IP + 平文 HTTP）ことが明確

## 依存と後続

- 依存: Plan 01 (TCP), Plan 02 (HTTP), Plan 03 (JSON)
- 後続: Plan 08 (TLS で同じテストを HTTPS に切り替える), Plan 10 (Claude アダプタ)

---

## 技術調査結果

### A. QEMU 自動テスト終了 (isa-debug-exit)

ゲストコードから I/O ポートに書き込むことで QEMU プロセスを終了させる仮想デバイス。

#### QEMU コマンドライン

```bash
qemu-system-i386 \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -fda build/bin/fsboot.bin \
  -serial file:build/log/test_serial.log \
  -display none -nographic
```

#### 終了コード計算式

```
QEMU exit code = (val << 1) | 1
```

| 書き込み値 | QEMU exit code |
|-----------|---------------|
| 0x00 | 1 |
| 0x01 | 3 |

exit code 0 は不可能（常にビット0が1）。テストハーネスでは exit code = 1 を成功と扱う。

#### ゲスト側コード

```c
#define QEMU_EXIT_PORT  0xf4

PRIVATE void qemu_exit(int code) {
    __asm__ __volatile__ (
        "outb %b0, %w1"
        :
        : "a"((unsigned char)code), "Nd"((unsigned short)QEMU_EXIT_PORT)
    );
}

#define QEMU_EXIT_SUCCESS()  qemu_exit(0x00)  /* → QEMU returns 1 */
#define QEMU_EXIT_FAILURE()  qemu_exit(0x01)  /* → QEMU returns 3 */
```

#### Makefile ターゲット例

```makefile
test-agent-bringup: all
	qemu-system-i386 \
	  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
	  -fda build/bin/fsboot.bin \
	  -serial file:$(LOGDIR)/test_serial.log \
	  -D $(LOGDIR)/test_qemu_debug.log \
	  -display none -nographic; \
	EXIT_CODE=$$?; \
	if [ $$EXIT_CODE -eq 1 ]; then \
	  echo "TEST PASSED"; exit 0; \
	else \
	  echo "TEST FAILED (exit code: $$EXIT_CODE)"; exit 1; \
	fi
```

### B. QEMU SLiRP でのゲスト→ホスト接続

- ゲストから `10.0.2.2:8080` への TCP 接続は SLiRP が `127.0.0.1:8080` に変換
- `hostfwd` はゲスト→ホスト方向には不要
- ARP request → SLiRP が即座に仮想 MAC で応答 → SYN 送信可能

### 参考資料

- [QEMU debugexit.c source](https://github.com/qemu/qemu/blob/master/hw/misc/debugexit.c)
- [QEMU Network Emulation](https://qemu.readthedocs.io/en/v10.0.3/system/devices/net.html)
