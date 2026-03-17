# Plan 18: エージェント結合テスト

## 概要

Plan 12–17 の全成果物を統合し、Sodex エージェントが自律的に OS を管理する
エンドツーエンドのデモを完成させる。モックと実 API の両方で、
複数ステップのエージェント実行、セッション永続化、権限制御を検証する。

## 目標

- 全コンポーネント（ツール実行、会話、エージェントループ、セッション、権限）を統合テストする
- 5 つ以上のリアルなタスクシナリオで自律実行が成立することを確認する
- パフォーマンス指標（レスポンス時間、トークン使用量）を計測する
- 実 Claude API での手動デモが完動する

## 実装済みスコープ（2026-03-17）

現在の実装では、QEMU 上で安定して再現できる最小の結合テストを先に完成させた。
`make -C src test-agent-full` は以下の 4 シナリオを対象とする。

- Scenario 1: immediate completion
- Scenario 2: one tool use (`read_file`) → completion
- Scenario 3: two-tool chain (`list_dir` → `read_file`) → completion
- Scenario 4: max steps 到達

この 4 シナリオは mock Claude server + TLS + SSE + agent loop を通して検証される。
一方で、当初 Plan に含めていた以下はまだ未了である。

- セッション再開シナリオ
- パフォーマンス集計の正式出力
- 実 Claude API を使った手動デモ

## 背景

Plan 11 が Phase C の結合テストであったように、本 Plan は Phase D–G の集大成となる。
「LLM API 駆動の自律 OS」というプロジェクトゴールの最初のマイルストーン達成を示す。

## 設計

### テストシナリオ

#### シナリオ 1: ファイル探索と報告

```
プロンプト: "Find all configuration files in /etc and summarize their contents"

期待される動作:
  Step 1: Claude → list_dir(/etc) → ファイル一覧取得
  Step 2: Claude → read_file(/etc/hostname) → 内容取得
  Step 3: Claude → read_file(/etc/claude.conf) → 内容取得
  Step 4: Claude → テキスト応答（サマリ）
```

#### シナリオ 2: システム診断

```
プロンプト: "Check the system health and report any issues"

期待される動作:
  Step 1: Claude → get_system_info(all) → メモリ/プロセス/デバイス情報
  Step 2: Claude → テキスト応答（診断レポート）

  異常検知時:
  Step 3: Claude → manage_process(list) → プロセス詳細
  Step 4: Claude → テキスト応答（推奨アクション）
```

#### シナリオ 3: ファイル作成と検証

```
プロンプト: "Create a configuration file at /home/test.conf with network settings, then verify it was written correctly"

期待される動作:
  Step 1: Claude → write_file(/home/test.conf, ...) → ファイル作成
  Step 2: Claude → read_file(/home/test.conf) → 内容検証
  Step 3: Claude → テキスト応答（確認報告）
```

#### シナリオ 4: 権限ブロックとリカバリ

```
プロンプト: "Write a backup of /etc/hostname to /boot/hostname.bak"

期待される動作:
  Step 1: Claude → read_file(/etc/hostname) → 内容取得
  Step 2: Claude → write_file(/boot/hostname.bak) → 権限ブロック
  Step 3: Claude → write_file(/home/hostname.bak) → 代替パスで成功
  Step 4: Claude → テキスト応答（/boot は保護されている旨の説明）
```

#### シナリオ 5: セッション再開

```
1回目: "What files are in /etc?"
  → list_dir 実行、結果表示
  → セッション保存

2回目（resume）: "Read the first file you found"
  → 前回の会話を復元
  → 前回 list_dir の結果を記憶した状態で read_file を実行
```

### モックサーバのマルチシナリオ対応

```python
# tests/mock_claude_server.py — 拡張
#
# リクエストの messages 内容に基づいてシナリオを分岐:
#
# /v1/messages?scenario=1  — ファイル探索シナリオ
# /v1/messages?scenario=2  — システム診断シナリオ
# /v1/messages?scenario=3  — ファイル作成シナリオ
# /v1/messages?scenario=4  — 権限ブロックシナリオ
#
# または messages の内容を解析してステートマシンで応答:
#
# 初回（tool_result なし）→ tool_use を返す
# tool_result あり → 次の tool_use またはテキスト応答
```

### 自動テストの構成

```makefile
# Makefile ターゲット
test-agent-full:
	@echo "=== Agent Integration Test ==="
	python3 tests/run_agent_integration.py

# run_agent_integration.py:
#   1. モックサーバ起動（TLS + 全シナリオ対応）
#   2. QEMU 起動、rootfs に agent コマンド組み込み
#   3. シナリオ 1–4 を順次実行（シリアル経由でコマンド投入）
#   4. シリアルログから PASS/FAIL 判定
#   5. パフォーマンス計測
#   6. クリーンアップ
```

### テストエントリ

```c
/* src/usr/command/agent.c — テストサブコマンド */

void cmd_agent_integration_test(void)
{
    int passed = 0, failed = 0;

    com1_printf("[AGENT-INTEG] === Integration Test Start ===\n");

    /* シナリオ 1: ファイル探索 */
    com1_printf("[AGENT-INTEG] scenario 1: file exploration... ");
    struct agent_result r1;
    int err = agent_run(&test_config,
        "List the files in /etc and read the hostname file", &r1);
    if (!err && r1.stop_reason == AGENT_STOP_END_TURN
            && r1.total_tool_calls >= 2) {
        com1_printf("PASS (steps=%d, tools=%d, tokens=%d)\n",
            r1.steps_executed, r1.total_tool_calls,
            r1.total_input_tokens + r1.total_output_tokens);
        passed++;
    } else {
        com1_printf("FAIL (err=%d, stop=%d, tools=%d)\n",
            err, r1.stop_reason, r1.total_tool_calls);
        failed++;
    }

    /* シナリオ 2–4: 同様 */
    /* ... */

    /* シナリオ 5: セッション再開 */
    com1_printf("[AGENT-INTEG] scenario 5: session resume... ");
    /* 1回目の実行 */
    struct agent_result r5a;
    agent_run(&test_config, "What files are in /etc?", &r5a);
    char session_id[33];
    /* r5a からセッション ID を取得 */
    /* 2回目: resume */
    test_config_resume.resume_session_id = session_id;
    struct agent_result r5b;
    agent_run(&test_config_resume, "Read the first file", &r5b);
    /* ... 検証 ... */

    com1_printf("[AGENT-INTEG] === Results: %d passed, %d failed ===\n",
        passed, failed);

    if (failed == 0)
        com1_printf("[AGENT-INTEG] ALL PASSED\n");
    else
        com1_printf("[AGENT-INTEG] FAILED\n");
}
```

### パフォーマンス指標

```
[AGENT-INTEG] Performance Summary:
  Scenario 1: 3 steps, 4200ms total, 2 API calls, 345 tokens
  Scenario 2: 2 steps, 2800ms total, 1 API call, 210 tokens
  Scenario 3: 3 steps, 5100ms total, 3 API calls, 420 tokens
  Scenario 4: 4 steps, 6300ms total, 4 API calls, 580 tokens
  Scenario 5: 2+2 steps, 7200ms total, 3+2 API calls, 890 tokens

  Average step time: 1250ms
  Average tokens per scenario: 489
  Total test time: 25600ms
```

## 実装ステップ

1. `tests/run_agent_integration.py` — テストオーケストレーターを作成する
2. モックサーバに 5 シナリオのレスポンスパターンを追加する
3. `agent.c` に `cmd_agent_integration_test()` を実装する
4. シナリオ 1（ファイル探索）の実装と検証
5. シナリオ 2（システム診断）の実装と検証
6. シナリオ 3（ファイル作成）の実装と検証
7. シナリオ 4（権限ブロック）の実装と検証
8. シナリオ 5（セッション再開）の実装と検証
9. パフォーマンス計測と出力
10. `make test-agent-full` ターゲットを作成する
11. 実 API での手動デモを確認する

## テスト

### 自動テスト (`make test-agent-full`)

- モック使用、現状は 4 シナリオ全 PASS
- タイムアウト: 120 秒（i486 の TLS 速度 × 複数 API 往復）
- シリアルログの `ALL PASSED` で判定

### 手動テスト（実 API）

- `agent run "Check my system and create a status report"` を実行
- ストリーミング出力の確認
- セッション永続化の確認
- 監査ログの確認

### リグレッションテスト

- Phase A–C のテスト（`make test-agent-bringup`, `make test-claude-smoke`）が引き続き PASS

## 変更対象

- 新規:
  - `tests/run_agent_integration.py`
  - `tests/fixtures/agent_integration/` — シナリオ別レスポンスデータ
- 既存:
  - `src/usr/command/agent.c` — 結合テストサブコマンド
  - `tests/mock_claude_server.py` — マルチシナリオ対応
  - `src/makefile` — `test-agent-full` ターゲット

## 完了条件

- 5 シナリオのモック結合テストが `make test-agent-full` で全 PASS
- セッション再開テストが動作する
- 権限ブロックテストが動作する
- パフォーマンス指標が出力される
- 実 API で手動デモが完動する
- Phase A–C のリグレッションテストが PASS

## 依存と後続

- 依存: Plan 12–17（Phase D–G の全 Plan）
- 後続: 自律 OS としての高度な機能拡張（定期タスク、イベント駆動エージェント等）

## 実装メモ: 1460B 超の TLS 送信失敗

`test-agent-full` の初期実装では、
1 回目の API call は通るが 2 回目以降の大きい request body で
`TLS_ERR_RECV(-7)` が出る不具合があった。

現象としては、`body <= 1300B` 前後は通り、
`body > 1460B` 付近から mock server 側で
`ssl.SSLError: RECORD_LAYER_FAILURE` が再現した。

### 誤りだった仮説

当初は「Python SSL サーバが TCP 分割された TLS record を再組み立てできない」
と考えたが、これは誤りだった。

- 通常の HTTPS client から大きい POST を送っても mock server は正常応答する
- TLS / SSL 自体が TCP 分割を前提にしている

### 実際の原因

原因は Sodex 側の TCP 送信実装にあった。

1. `kern_sendto()` が `SOCK_TXBUF_SIZE=1460` で chunking していた
2. しかし uIP が実際に送れる長さは `conn->mss`（約 1446B）で、内部で切り詰められる
3. その結果、1460B を積んだつもりでも末尾の一部が欠落し、TLS ciphertext が壊れる
4. さらに ACK 前でも `tx_buf` / `tx_len` を再利用でき、outstanding data の再送元も不安定だった

### 実施した修正

- `src/socket.c` で TCP 送信 chunk を `tcp_conn->mss` 以下に制限
- 次チャンク投入前に `tx_pending` だけでなく `uip_outstanding()` も待つよう修正
- 最終チャンクも ACK 済みになるまで待ってから `send_msg()` を返すよう修正

### 検証結果

- `make -C src test-agent-bringup` → 18/18 PASS
- `make -C src test-claude-smoke` → 3/3 PASS
- `make -C src test-agent-full` → 4/4 PASS

---

## マイルストーン

この Plan が完了すると、プロジェクトの中核ゴール:

> Sodex が LLM API 駆動で自律的に OS 管理タスクを実行する

が達成される。以降は以下の方向に拡張可能:

1. **定期実行エージェント**: cron 的にエージェントを起動して定期診断
2. **イベント駆動エージェント**: 異常検知時に自動でエージェントを起動
3. **ホスト連携**: virtio-serial 経由で Agent SDK のフル機能を利用
4. **マルチエージェント**: 異なるシステムプロンプトの専門エージェント
