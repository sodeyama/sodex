# Plan 14: エージェントループ

## 概要

Claude Agent SDK の中核である「エージェントループ」をユーザランドで実装する。
ユーザーのプロンプトを受け取り、Claude API に問い合わせ、ツール呼び出しがあれば
自動的に実行し、最終応答に到達するまで自律的にループを回す。
Sodex が LLM 駆動の自律 OS として機能するための核心部分。

## 目標

- プロンプトを受け取り、最終テキスト応答を返すまでの全ループを自律的に実行する
- Claude Agent SDK の `stopWhen: stepCountIs(N)` に相当する停止条件を実装する
- ループ中の各ステップを診断ログに出力し、デバッグ可能にする
- エラーリカバリ（API 失敗、ツール実行失敗）を安全に処理する
- `agent` コマンドの拡張として自律エージェントモードを提供する

## 背景

Claude Agent SDK のエージェントループは以下の構造:

```
1. ユーザープロンプト受信
2. Claude API にメッセージ送信
3. レスポンス解析
4. stop_reason == "end_turn" → 完了
5. stop_reason == "tool_use" → ツール実行 → tool_result を会話に追加 → 2 に戻る
6. stop_reason == "max_tokens" → エラーまたは継続
7. 最大ターン超過 → 強制停止
```

SDK のデフォルト最大ステップは 20。Sodex ではリソース制約を考慮して 10 をデフォルトとする。

## 設計

### エージェント設定

```c
/* src/usr/include/agent/agent.h */

#define AGENT_DEFAULT_MAX_STEPS     10
#define AGENT_DEFAULT_MAX_TOKENS  4096
#define AGENT_MAX_SYSTEM_PROMPT   4096
#define AGENT_MAX_RESPONSE        8192

/* 停止条件 */
enum agent_stop_condition {
    AGENT_STOP_END_TURN,          /* Claude が自発的に停止（正常完了） */
    AGENT_STOP_MAX_STEPS,         /* 最大ステップ数に到達 */
    AGENT_STOP_SPECIFIC_TOOL,     /* 特定のツールが呼ばれた */
    AGENT_STOP_ERROR,             /* 回復不能エラー */
    AGENT_STOP_TOKEN_LIMIT,       /* トークン上限に到達 */
};

/* エージェント設定 */
struct agent_config {
    const char *model;                           /* "claude-sonnet-4-20250514" */
    char system_prompt[AGENT_MAX_SYSTEM_PROMPT];
    int  system_prompt_len;

    int  max_steps;                 /* デフォルト: 10 */
    int  max_tokens_per_turn;       /* デフォルト: 4096 */

    /* 停止条件 */
    const char *terminal_tool;      /* NULL = 無効。指定するとこのツール呼び出しで停止 */

    /* API 認証 */
    const char *api_key;
};

/* エージェント実行時の状態 */
struct agent_state {
    struct conversation conv;
    int  current_step;
    enum agent_stop_condition stop_reason;

    /* 統計 */
    int  total_api_calls;
    int  total_tool_executions;
    int  total_errors;
    int  elapsed_ms;                /* 全体の実行時間 */
};

/* エージェント実行結果 */
struct agent_result {
    enum agent_stop_condition stop_reason;
    char final_text[AGENT_MAX_RESPONSE];
    int  final_text_len;

    /* 統計サマリ */
    int  steps_executed;
    int  total_input_tokens;
    int  total_output_tokens;
    int  total_tool_calls;
    int  elapsed_ms;
};
```

### エージェントループ API

```c
/* メインループ: プロンプトを受け取り、最終応答まで自律実行 */
int agent_run(
    const struct agent_config *config,
    const char *initial_prompt,
    struct agent_result *result
);

/* ループの 1 ステップを実行（テスト用に公開） */
int agent_step(
    const struct agent_config *config,
    struct agent_state *state,
    struct claude_response *resp
);
```

### エージェントループの擬似コード

```
agent_run(config, prompt, result):
    agent_state_init(&state, config)
    conv_init(&state.conv, config->system_prompt)
    conv_add_user_text(&state.conv, prompt)
    tool_registry_init_defaults()

    for step = 0; step < config->max_steps; step++:
        state.current_step = step
        log("[AGENT] step %d/%d", step + 1, config->max_steps)

        // Claude API に会話を送信
        err = claude_send_conversation(&provider_claude, &state.conv,
                                       config->api_key, &resp)
        if err:
            if is_retryable(err) && step < config->max_steps - 1:
                log("[AGENT] API error %d, retrying...", err)
                continue
            result->stop_reason = AGENT_STOP_ERROR
            return -1

        state.total_api_calls++

        // アシスタントの応答を会話に追加（conv が自動管理）
        conv_add_assistant_response(&state.conv, &resp)

        // stop_reason で分岐
        switch resp.stop_reason:

        case CLAUDE_STOP_END_TURN:
            // 最終テキスト応答を抽出
            extract_final_text(&resp, result)
            result->stop_reason = AGENT_STOP_END_TURN
            log("[AGENT] completed: %d steps, %d tokens",
                step + 1, conv_token_count(&state.conv))
            return 0

        case CLAUDE_STOP_TOOL_USE:
            // 全 tool_use ブロックを実行
            for each tool_use_block in resp.blocks:
                if block.type != CLAUDE_CONTENT_TOOL_USE:
                    continue

                // terminal_tool チェック
                if config->terminal_tool &&
                   strcmp(block.tool_use.name, config->terminal_tool) == 0:
                    result->stop_reason = AGENT_STOP_SPECIFIC_TOOL
                    return 0

                log("[AGENT] executing tool: %s", block.tool_use.name)
                tool_dispatch(&block.tool_use, &tool_result)
                state.total_tool_executions++

                if tool_result.is_error:
                    state.total_errors++
                    log("[AGENT] tool error: %s", tool_result.content)

            // tool_result を会話に追加
            conv_add_tool_results(&state.conv, tool_results, tool_count)

            // トークン上限チェック
            if conv_check_tokens(&state.conv) == CONV_TOKEN_STOP:
                result->stop_reason = AGENT_STOP_TOKEN_LIMIT
                return -1

            continue  // 次のステップ

        case CLAUDE_STOP_MAX_TOKENS:
            log("[AGENT] max_tokens reached, truncated response")
            // 切り詰められた応答を保存して継続を試みる
            // Claude は次のターンで続きを生成できる
            continue

    // ループ終了
    result->stop_reason = AGENT_STOP_MAX_STEPS
    log("[AGENT] max steps (%d) reached", config->max_steps)
    return 0
```

### エラーリカバリ戦略

```
エラー種別ごとの対応:

API エラー (HTTP 5xx/529):
  → claude_client.c の既存リトライロジック（Plan 10）が処理
  → 全リトライ失敗 → agent_run に -1 を返す

API エラー (HTTP 429):
  → 既存リトライロジックが retry-after に従う
  → 全リトライ失敗 → ステップを消費して次のループで再試行

ツール実行エラー:
  → is_error: true の tool_result を Claude に返す
  → Claude がリカバリを判断（別のツール試行 or ユーザーに説明）
  → エージェントは停止しない

ツール未登録:
  → "Unknown tool: xxx" を is_error: true で返す
  → Claude が別の方法を試みる

トークン超過:
  → 古いターンの切り捨て（Plan 13）
  → 切り捨て後も超過 → 強制停止

会話ターン数超過:
  → 最大ステップ停止（正常終了扱い）
```

### 診断ログフォーマット

```
[AGENT] === Agent Run Start ===
[AGENT] model=claude-sonnet-4-20250514, max_steps=10
[AGENT] system_prompt=148 bytes, tools=5 registered
[AGENT] step 1/10
  [HTTP] POST /v1/messages (body=1234 bytes)
  [SSE] event: message_start
  [SSE] event: content_block_start (type=tool_use, name=read_file)
  [SSE] event: message_delta (stop_reason=tool_use)
  [CLAUDE] response: 1 tool_use, stop=tool_use, tokens=50/30
[AGENT] executing tool: read_file (input={"path":"/etc/hostname"})
[AGENT] tool result: 12 bytes, is_error=false
[AGENT] step 2/10
  [HTTP] POST /v1/messages (body=1890 bytes)
  [SSE] event: message_start
  [SSE] event: content_block_delta (text="The hostname is...")
  [SSE] event: message_delta (stop_reason=end_turn)
  [CLAUDE] response: 1 text, stop=end_turn, tokens=120/45
[AGENT] completed: 2 steps, 245 total tokens, 1 tool calls, 3400ms
[AGENT] === Agent Run End ===
```

### agent コマンドの拡張

```c
/* src/usr/command/agent.c — 拡張 */

/*
 * 使い方:
 *   agent run "タスクの説明"          — 自律実行
 *   agent run -s 5 "タスク"           — 最大 5 ステップ
 *   agent run -t final_answer "タスク" — final_answer ツールで停止
 *   agent test                         — Phase A のブリングアップテスト（既存）
 */
```

## 実装ステップ

1. `agent.h` にエージェント設定・状態・結果の構造体を定義する
2. `agent.c` に `agent_run()` メインループを実装する
3. `agent_step()` を切り出してテスタブルにする
4. エラーリカバリ: API エラー時の再試行とツールエラー時の is_error 返送
5. 停止条件の実装: end_turn, max_steps, specific_tool, token_limit
6. 診断ログの各ステップ出力を実装する
7. `agent` コマンドを拡張し `agent run` サブコマンドを追加する
8. 統計サマリ（ステップ数、トークン、ツール呼び出し回数、実行時間）を実装する
9. host 単体テストを書く
10. モックサーバに 3 ステップのエージェントシナリオを追加する
11. QEMU で完全なエージェントループを確認する

## テスト

### host 単体テスト (`tests/test_agent_loop.c`)

- 1 ステップ完了: prompt → end_turn → `stop_reason == AGENT_STOP_END_TURN`
- 2 ステップ: prompt → tool_use → tool_result → end_turn
- max_steps 停止: 3 回連続 tool_use → `stop_reason == AGENT_STOP_MAX_STEPS`
- terminal_tool 停止: `final_answer` ツール呼び出し → 即停止
- ツールエラー: is_error=true の tool_result が Claude に返る
- API エラー: 500 → リトライ → 成功（stub で確認）

### QEMU スモーク

モックサーバに以下のシナリオを追加:

- **シナリオ A: 即時完了** — prompt → テキスト応答（end_turn）
- **シナリオ B: 1 ツール実行** — prompt → tool_use(read_file) → tool_result 受信 → テキスト応答
- **シナリオ C: 2 ツール連鎖** — prompt → tool_use(list_dir) → tool_result → tool_use(read_file) → tool_result → テキスト応答
- **シナリオ D: max_steps 停止** — 4 回連続 tool_use（max_steps=3 で停止確認）

各シナリオの判定: シリアルログの `[AGENT] completed` または `[AGENT] max steps` を grep。

### fixture (`tests/fixtures/agent/`)

- `scenario_a_responses.json` — 即時完了レスポンス列
- `scenario_b_responses.json` — 1 ツールレスポンス列
- `scenario_c_responses.json` — 2 ツール連鎖レスポンス列
- `scenario_d_responses.json` — max_steps テスト用レスポンス列

## 変更対象

- 新規:
  - `src/usr/include/agent/agent.h`
  - `src/usr/lib/libagent/agent.c`
  - `tests/test_agent_loop.c`
  - `tests/fixtures/agent/`
- 既存:
  - `src/usr/command/agent.c` — `agent run` サブコマンド追加
  - `tests/mock_claude_server.py` — マルチステップシナリオ追加

## 完了条件

- `agent run "タスク"` で最終テキスト応答まで自律的にループが回る
- tool_use → 実行 → tool_result → 再問い合わせの自動サイクルが動作する
- max_steps で安全に停止する
- ツール実行エラーが Claude にフィードバックされる
- 全ステップの診断ログがシリアルに出力される
- host 単体テストと QEMU スモークテスト 4 シナリオが PASS

## 依存と後続

- 依存: Plan 12 (ツール実行), Plan 13 (マルチターン会話)
- 後続: Plan 15 (システムプロンプト設計), Plan 16 (セッション永続化)

---

## 技術調査結果

### A. Claude Agent SDK のエージェントループ仕様

SDK の `query()` 関数は以下のフラグで CLI を起動する:
```
claude --print --input-format stream-json --output-format stream-json
```

SDK 側のループ:
1. CLI がプロンプトを受け取る
2. Claude API に送信
3. レスポンスの tool_use をCLI内で実行（Read/Write/Bash等）
4. tool_result を自動で Claude に返す
5. end_turn まで繰り返す

Sodex の実装では SDK/CLI のレイヤーをスキップし、API を直接叩くため、
ツール実行の責任はエージェントループが直接持つ。

### B. 停止条件の設計パターン

SDK の `ToolLoopAgent`:
- `stopWhen: stepCountIs(N)` — N ステップで停止
- `stopWhen: hasToolCall("final_answer")` — 特定ツールで停止
- `stopWhen` は関数型で任意の条件を定義可能

Sodex では列挙型ベースの固定条件セットとする（C の関数ポインタで拡張可能）。

### C. エラーハンドリングのベストプラクティス

Anthropic 推奨:
- ツールエラーは `is_error: true` で返し、Claude に判断を委ねる
- `content` フィールドを空にしない（400 エラーになる）
- エラーメッセージに具体的な情報を含める

### 参考資料

- [Anthropic Tool Use](https://platform.claude.com/docs/en/docs/build-with-claude/tool-use)
- [Agent SDK Agent Loop](https://platform.claude.com/docs/en/agent-sdk/agent-loop)
- [Agent SDK Quickstart](https://platform.claude.com/docs/en/agent-sdk/quickstart)
