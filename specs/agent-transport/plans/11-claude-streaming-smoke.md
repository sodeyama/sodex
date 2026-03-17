# Plan 11: Claude ストリーミング結合テスト

## 概要

Plan 01–10 の全成果物を統合し、QEMU 上の Sodex から Claude API に
HTTPS SSE ストリーミングで問い合わせ、応答を表示するデモを完成させる。

## 目標

- 実 API またはモックで Claude Messages API のストリーミング応答を受信・表示する
- tool_use が返った場合に内部表現への変換まで確認する（実行は後続 EPIC）
- 全レイヤーの結合がシリアルログで追跡できる
- エラー時にどのレイヤーで止まったか診断できる

## 設計

### テスト構成

2 つのモードで結合テストを行う:

**モード A: モック Claude サーバ（自動テスト用）**
```
QEMU → DNS (10.0.2.3) → 10.0.2.2:4443 (ホスト側 TLS モック)
          ↓
     TLS + HTTP POST /v1/messages
          ↓
     SSE ストリーミング応答 (固定シナリオ)
```

- `tests/mock_claude_server.py` を拡張し、TLS 対応 + SSE ストリーミングを返す
- 自己署名証明書 + ピンニングで TLS を通す
- テキスト応答、tool_use 応答、エラー応答の 3 シナリオ

**モード B: 実 Claude API（手動確認用）**
```
QEMU → DNS (10.0.2.3) → api.anthropic.com:443
          ↓
     TLS + HTTP POST /v1/messages
          ↓
     SSE ストリーミング応答 (実 LLM)
```

- API キーをビルド時に埋め込み（または起動時注入）
- 手動実行のみ。CI には入れない

### テストエントリ

```c
/* src/agent/claude_smoke_test.c */

void claude_smoke_test(void)
{
    struct claude_response resp;
    int err;

    com1_printf("[CLAUDE-TEST] === Phase A: Mock Claude ===\n");

    /* テスト 1: テキスト応答 */
    com1_printf("[CLAUDE-TEST] 1. text response... ");
    err = claude_send_message(
        &provider_claude,
        "Say hello in one word.",
        &resp
    );
    if (err) { com1_printf("FAIL: %d\n", err); return; }
    if (resp.stop_reason != CLAUDE_STOP_END_TURN) {
        com1_printf("FAIL: unexpected stop_reason=%d\n", resp.stop_reason);
        return;
    }
    com1_printf("OK: \"%s\"\n", resp.blocks[0].text.text);

    /* テスト 2: tool_use 応答 */
    com1_printf("[CLAUDE-TEST] 2. tool_use response... ");
    err = claude_send_message_with_tools(
        &provider_claude,
        "Read the file /etc/hostname",
        test_tools, test_tool_count,
        &resp
    );
    if (err) { com1_printf("FAIL: %d\n", err); return; }
    if (!claude_needs_tool_call(&resp)) {
        com1_printf("FAIL: expected tool_use\n");
        return;
    }
    com1_printf("OK: tool=%s, id=%s\n",
        resp.blocks[1].tool_use.name,
        resp.blocks[1].tool_use.id);

    /* テスト 3: エラーハンドリング */
    com1_printf("[CLAUDE-TEST] 3. error handling... ");
    /* 429 シナリオ → リトライ → 200 */

    com1_printf("[CLAUDE-TEST] ALL PASSED\n");
}
```

### 診断ログの全レイヤー出力

```
[DNS] resolving mock-claude.local → 10.0.2.2 (5ms, cached)
[TCP] connecting to 10.0.2.2:4443 ... OK (120ms)
[TLS] handshake: cipher=ChaCha20-Poly1305 (2500ms)
[TLS] cert pin: OK
[HTTP] POST /v1/messages (body=342 bytes)
[HTTP] response: 200 OK, Content-Type: text/event-stream
[SSE] event: message_start
[SSE] event: content_block_start (type=text)
[SSE] event: content_block_delta (text="Hello")
[SSE] event: content_block_stop
[SSE] event: message_delta (stop_reason=end_turn)
[SSE] event: message_stop
[CLAUDE] response complete: 1 block, stop=end_turn, tokens=5/1
```

### 高レベル API

```c
/* src/agent/claude_client.c — Plan 01–10 を結合した高レベル関数 */

/* 単純なメッセージ送信 + ストリーミング受信 */
int claude_send_message(
    const struct llm_provider *provider,
    const char *user_message,
    struct claude_response *out
);

/* ツール定義付きメッセージ送信 */
int claude_send_message_with_tools(
    const struct llm_provider *provider,
    const char *user_message,
    const struct tool_def *tools, int tool_count,
    struct claude_response *out
);

/* 会話履歴付きメッセージ送信（tool_result 返却用） */
int claude_send_messages(
    const struct llm_provider *provider,
    const struct claude_message *msgs, int msg_count,
    const struct tool_def *tools, int tool_count,
    struct claude_response *out
);
```

内部フロー:
```
claude_send_message()
  → claude_build_request() [Plan 10]
  → http_do_request() with use_tls=1 [Plan 02 + 08]
    → dns_resolve() [Plan 05]
    → tls_connect() [Plan 08]
    → tls_send(request) [Plan 08]
    → recv loop:
      → tls_recv(chunk) [Plan 08]
      → sse_feed(chunk, &event) [Plan 09]
      → claude_parse_sse_event(&event, &response) [Plan 10]
    → tls_close() [Plan 08]
  → return response
```

## 実装ステップ

1. `tests/mock_claude_server.py` を拡張して TLS + SSE 対応にする
2. テスト用自己署名証明書を生成し、公開鍵ハッシュをテストコードに埋め込む
3. `claude_client.c` に高レベル API を実装する
4. `claude_smoke_test.c` にテストシナリオを実装する
5. `makefile` に `make test-claude-smoke` ターゲットを追加する
   - モック起動 → QEMU 起動 → シリアルから PASS/FAIL 判定 → クリーンアップ
6. 手動モード用に `make run-claude-live` ターゲットを追加する（実 API キー使用）

## テスト

### 自動テスト (`make test-claude-smoke`)

- モード A（モック）で 3 シナリオを実行
- シリアル出力から `ALL PASSED` を grep
- タイムアウト 60 秒（TLS ハンドシェイクの i486 速度を考慮）

### 手動テスト

- 実 Claude API にプロンプト送信
- ストリーミングテキストがシリアルにリアルタイム表示される
- VGA にも表示される（オプション）

## 変更対象

- 新規:
  - `src/agent/claude_client.c`
  - `src/include/agent/claude_client.h`
  - `src/agent/claude_smoke_test.c`
  - `tests/mock_claude_server.py` (拡張)
  - `tests/certs/` (テスト用自己署名証明書)
- 既存:
  - `src/kernel.c` — テスト分岐追加
  - `src/makefile` — テストターゲット追加

## 完了条件

- モックサーバからの SSE ストリーミング応答を受信・パースできる
- tool_use ブロックを内部表現に変換できる
- `make test-claude-smoke` が 1 コマンドで通る
- 全レイヤーの診断ログがシリアルに出る
- MCP 未実装でも Claude 単独デモが成立する

## 依存と後続

- 依存: Plan 01–10 の全 Plan
- 後続: `specs/stateless-agent-os/` EPIC-05 (MCP), EPIC-06 (Agent Loop)

## マイルストーン

この Plan が完了すると、レポートの **Phase 2 完了条件**:

> `POST https://api.anthropic.com/v1/messages` → ストリーミング応答を表示

が達成される。以降は MCP/Capability/Agent Loop の実装に進む。

---

## 技術調査結果

### A. Python での TLS 対応 SSE モックサーバ実装

#### 自己署名証明書の生成

```bash
openssl req -new -newkey rsa:2048 -x509 -sha256 -days 365 -nodes \
  -out cert.pem -keyout key.pem -subj "/CN=localhost"
```

#### TLS + SSE モックサーバ (抜粋)

```python
import http.server, ssl, json, time

class SSEHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length)
        request = json.loads(body)

        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Cache-Control', 'no-cache')
        self.end_headers()

        # message_start
        self._send_sse('message_start', json.dumps({
            "type": "message_start",
            "message": {"id": "msg_test", "type": "message",
                        "role": "assistant", "content": [],
                        "model": "mock", "stop_reason": None,
                        "usage": {"input_tokens": 10, "output_tokens": 1}}
        }))
        # content_block_delta (チャンク送信)
        for word in ["Hello", " from", " mock!"]:
            time.sleep(0.1)
            self._send_sse('content_block_delta', json.dumps({
                "type": "content_block_delta", "index": 0,
                "delta": {"type": "text_delta", "text": word}
            }))
        # message_stop
        self._send_sse('message_stop', '{"type":"message_stop"}')

    def _send_sse(self, event, data):
        payload = f"event: {event}\ndata: {data}\n\n"
        self.wfile.write(payload.encode())
        self.wfile.flush()

context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain('cert.pem', 'key.pem')
server = http.server.HTTPServer(('0.0.0.0', 8443), SSEHandler)
server.socket = context.wrap_socket(server.socket, server_side=True)
server.serve_forever()
```

### B. QEMU 自動テスト終了 (isa-debug-exit)

```bash
qemu-system-i386 \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -fda build/bin/fsboot.bin \
  -serial file:build/log/test_serial.log \
  -display none -nographic
```

ゲスト側: `outb(0xf4, 0x00)` → QEMU exit code = 1 (成功)
ゲスト側: `outb(0xf4, 0x01)` → QEMU exit code = 3 (失敗)

テスト Makefile で exit code 1 を成功と判定。

### C. api.anthropic.com の TLS 接続情報 (2026年3月実測)

- 発行者: Google Trust Services (GTS), CN=WE1
- 鍵: EC prime256v1 (P-256)
- TLS 1.2 フォールバック: ECDHE-ECDSA-CHACHA20-POLY1305 対応
- BearSSL で接続可能（TLS 1.3 非対応だが TLS 1.2 で問題なし）

### D. テスト戦略まとめ

| テスト | 方式 | 自動化 |
|--------|------|--------|
| モック TLS + SSE | Python サーバ + 自己署名証明書 | `make test-claude-smoke` |
| 実 API | API キー埋め込み | 手動のみ (`make run-claude-live`) |
| テスト終了 | isa-debug-exit | exit code で PASS/FAIL 判定 |
| タイムアウト | 60秒 | TLS ハンドシェイクの i486 速度考慮 |

### 参考資料

- [Python ssl module](https://docs.python.org/3/library/ssl.html)
- [QEMU debugexit.c](https://github.com/qemu/qemu/blob/master/hw/misc/debugexit.c)
- [Anthropic Messages API](https://platform.claude.com/docs/en/api/messages)
