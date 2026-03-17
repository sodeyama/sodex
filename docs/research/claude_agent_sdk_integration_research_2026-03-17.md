# Claude Agent SDK 調査レポート: Sodexへの取り込み戦略

**調査日**: 2026-03-17
**目的**: Claude Agent SDK（旧Claude Code SDK）の技術的詳細を調査し、Sodex独自OSカーネルへの統合方法を検討する

---

## 1. エグゼクティブサマリー

### Claude Agent SDKとは

Claude Agent SDK（`@anthropic-ai/claude-agent-sdk` / `claude-agent-sdk`）は、Claude Code CLIと同じAIエージェントエンジンをプログラマティックに利用するためのライブラリである。TypeScript/Pythonから利用でき、ファイル読み書き、コマンド実行、Web検索、コード編集などのツールを自律的に実行するエージェントを構築できる。

### Sodexへの統合に関する結論

| アプローチ | 実現可能性 | 推奨度 | 理由 |
|-----------|----------|--------|------|
| **A: Claude Messages API直接呼び出し（現行方式の拡張）** | ★★★★★ | **最推奨** | 既に実装済みの`ask`コマンドを拡張。カーネル内で完結 |
| **B: Agent SDKプロトコル（NDJSON over stdio）の再実装** | ★★★☆☆ | 条件付き推奨 | プロトコル自体はJSON/stdinベースで単純だが、subprocess管理が前提 |
| **C: Agent SDKのホスト側実行 + カーネルとのブリッジ** | ★★★★☆ | 推奨 | QEMUのvirtio-serial等でホストのSDKとカーネルを接続 |
| **D: Agent SDK をそのまま組み込む** | ★☆☆☆☆ | 非推奨 | Node.js/Pythonランタイムが必要。ベアメタルカーネルでは不可能 |

**推奨戦略**: **フェーズ分割アプローチ**
1. 短期（Phase D-E）: 現行Claude Messages APIの拡張（ツール呼び出し、マルチターン）
2. 中期（Phase F）: 独自エージェントループの実装（SDKの設計パターンを参考）
3. 長期（Phase G）: ホスト連携またはSDKプロトコル互換レイヤーの検討

---

## 2. Claude Agent SDK 技術詳細

### 2.1 パッケージ情報

| 項目 | TypeScript | Python |
|------|-----------|--------|
| パッケージ名 | `@anthropic-ai/claude-agent-sdk` | `claude-agent-sdk` |
| バージョン（2026年3月時点） | v0.2.71 | latest |
| リポジトリ | [github.com/anthropics/claude-agent-sdk-typescript](https://github.com/anthropics/claude-agent-sdk-typescript) | [github.com/anthropics/claude-agent-sdk-python](https://github.com/anthropics/claude-agent-sdk-python) |
| ランタイム要件 | Node.js | Python 3.10+ |
| Claude Code CLI | バンドル済み（別途インストール不要） | バンドル済み |

### 2.2 アーキテクチャ概要

```
┌─────────────────────────────────────────┐
│  ユーザーアプリケーション                    │
│  (TypeScript / Python)                  │
├─────────────────────────────────────────┤
│  Claude Agent SDK                       │
│  ├── query() / ClaudeSDKClient          │
│  ├── Hook System                        │
│  ├── Permission Manager                 │
│  └── Transport Layer                    │
├─────────────────────────────────────────┤
│  NDJSON over stdin/stdout (IPC)         │
├─────────────────────────────────────────┤
│  Claude Code CLI (subprocess)           │
│  ├── Agent Loop                         │
│  ├── Built-in Tools (Read/Write/Bash等) │
│  ├── MCP Client                         │
│  └── Anthropic API Client               │
├─────────────────────────────────────────┤
│  Claude API (api.anthropic.com)         │
└─────────────────────────────────────────┘
```

**重要な設計ポイント**: SDKはClaude Code CLIをサブプロセスとして起動し、NDJSON（改行区切りJSON）でstdin/stdoutを通じて通信する。REST APIではない。

### 2.3 通信プロトコル: NDJSON over stdio

CLIの起動コマンド:
```
claude --print --input-format stream-json --output-format stream-json
```

フラグの意味:
- `--print`: 非対話モード（応答後に終了）
- `--input-format stream-json`: stdinでNDJSON入力を期待
- `--output-format stream-json`: stdoutにNDJSON出力

### 2.4 メッセージ型定義

SDKが扱う5つのメッセージ型:

#### (1) SystemMessage — セッション初期化

```json
{
  "type": "system",
  "subtype": "init",
  "session_id": "uuid-here",
  "data": {
    "session_id": "uuid-here",
    "model": "claude-opus-4-6",
    "tools": ["Read", "Write", "Edit", "Bash", "Glob", "Grep"]
  }
}
```

#### (2) AssistantMessage — Claudeの応答

```json
{
  "type": "assistant",
  "content": [
    {"type": "text", "text": "ファイルを確認します..."},
    {
      "type": "tool_use",
      "id": "tool_abc123",
      "name": "Read",
      "input": {"file_path": "/etc/config"}
    },
    {"type": "thinking", "text": "...内部思考..."}
  ],
  "model": "claude-opus-4-6",
  "parent_tool_use_id": null
}
```

#### (3) UserMessage — ツール実行結果

```json
{
  "type": "user",
  "tool_use_result": {
    "tool_use_id": "tool_abc123",
    "type": "tool_result",
    "content": [{"type": "text", "text": "ファイル内容..."}]
  }
}
```

#### (4) ResultMessage — 最終結果

```json
{
  "type": "result",
  "subtype": "success",
  "session_id": "uuid-here",
  "duration_ms": 5000,
  "duration_api_ms": 4500,
  "is_error": false,
  "num_turns": 3,
  "total_cost_usd": 0.0125,
  "usage": {
    "input_tokens": 1000,
    "output_tokens": 200,
    "cache_read_tokens": 0,
    "cache_creation_tokens": 0
  },
  "result": "最終テキスト結果"
}
```

#### (5) StreamEvent — トークンレベル更新

`include_partial_messages=True`時にリアルタイムトークン配信。

### 2.5 制御プロトコル（双方向）

対話モード（`ClaudeSDKClient`）では、CLI→SDK方向に制御リクエストが送信される:

**制御リクエスト（CLI → SDK）:**
```json
{
  "type": "control_request",
  "request_id": "req_123",
  "request": {
    "type": "permission",
    "tool_name": "Bash",
    "tool_input": {"command": "rm -rf /tmp/test"}
  }
}
```

**制御レスポンス（SDK → CLI）:**
```json
{
  "type": "control_response",
  "request_id": "req_123",
  "response": {
    "permissionDecision": "allow",
    "permissionDecisionReason": "Safe temp directory operation"
  }
}
```

`request_id`によるリクエスト-レスポンスの多重化で、複数の制御リクエストを並行処理可能。

### 2.6 ビルトインツール

| ツール名 | 機能 | Sodex対応状況 |
|---------|------|-------------|
| Read | ファイル読み取り | ext3fsで実装済み |
| Write | ファイル作成 | ext3fsで実装済み |
| Edit | ファイル編集（差分適用） | 未実装 |
| Bash | シェルコマンド実行 | シェル（基本）実装済み |
| Glob | パターンマッチによるファイル検索 | 未実装 |
| Grep | ファイル内容の正規表現検索 | 未実装 |
| WebSearch | Web検索 | 未実装（ネットワーク依存） |
| WebFetch | Webページ取得・解析 | HTTP GETは実装済み |
| AskUserQuestion | ユーザーへの質問 | 未実装 |
| Agent | サブエージェント起動 | 未実装 |

### 2.7 フックシステム

エージェントのライフサイクルにフックポイントを提供:

| イベント | タイミング | ブロック可能 |
|---------|----------|------------|
| PreToolUse | ツール実行前 | Yes |
| PostToolUse | ツール実行後 | No |
| UserPromptSubmit | プロンプト送信時 | Yes |
| Stop | エージェント停止時 | No |
| SubagentStart | サブエージェント起動時 | Yes |
| PermissionRequest | 権限要求時 | Yes |
| SessionStart | セッション開始時 | No |
| SessionEnd | セッション終了時 | No |

フックレスポンス形式:
```json
{
  "systemMessage": "追加のガイダンス",
  "continue": true,
  "hookSpecificOutput": {
    "hookEventName": "PreToolUse",
    "permissionDecision": "allow",
    "updatedInput": {}
  }
}
```

### 2.8 権限モデル

| モード | 動作 |
|-------|------|
| `default` | 不明なツールはコールバックで確認 |
| `acceptEdits` | ファイル操作を自動承認、他は確認 |
| `bypassPermissions` | 全ツール自動承認（サンドボックス向け） |
| `plan` | ツール実行なし、計画のみ |
| `dontAsk`（TS only） | 許可リスト外は拒否 |

評価順序: Hooks → Deny Rules → Permission Mode → Allow Rules → canUseTool callback

### 2.9 セッション管理

- セッションはディスク上のJSONLファイルとして永続化
- パス: `~/.claude/projects/<encoded-cwd>/<session-id>.jsonl`
- `session_id`はUUID4
- `resume`オプションでセッション再開可能
- `fork_session`で履歴を保持したまま分岐可能

### 2.10 MCP（Model Context Protocol）統合

```python
options = ClaudeAgentOptions(
    mcp_servers={
        "playwright": {
            "command": "npx",
            "args": ["@playwright/mcp@latest"]
        }
    }
)
```

MCPツールの命名規則: `mcp__<server_name>__<action>`

---

## 3. Sodex現行実装との比較

### 3.1 既に実装されている機能

Sodexの現行Agent Transport実装（Phase A-C完了）は、Claude Agent SDKの**コア通信層**に相当する機能を独自に実装済み:

| Agent SDK機能 | Sodex実装 | 状態 |
|-------------|----------|------|
| Claude API呼び出し | `claude_client.c` | ✅ 完了 |
| SSEストリーミング解析 | `sse_parser.c` | ✅ 完了 |
| APIリクエスト構築 | `claude_adapter.c` | ✅ 完了 |
| SSEイベント解析 | `claude_adapter.c` | ✅ 完了 |
| TLS暗号化通信 | `tls_client` (BearSSL) | ✅ 完了 |
| HTTPクライアント | `http_client` | ✅ 完了 |
| JSONパーサ/ライタ | `json.c` | ✅ 完了 |
| APIキー管理 | `/etc/claude.conf` | ✅ 完了 |
| プロバイダ抽象化 | `llm_provider.h` | ✅ 完了 |
| リトライロジック（429） | `claude_client.c` | ✅ 完了 |
| `tool_use`ブロック解析 | `claude_adapter.c` | ✅ 構造体定義済み |

### 3.2 未実装だがSDKが提供する機能

| 機能 | 重要度 | 実装難易度 | 備考 |
|------|--------|----------|------|
| ツール呼び出し応答（tool_result送信） | ★★★★★ | 中 | APIフォーマットに従ってJSONを構築するだけ |
| マルチターン会話 | ★★★★★ | 中 | メッセージ履歴の保持と送信 |
| エージェントループ | ★★★★★ | 高 | ツール実行→結果送信→再問い合わせの繰り返し |
| フックシステム | ★★★☆☆ | 中 | ツール実行前後のコールバック |
| 権限管理 | ★★★☆☆ | 低 | ツール実行の許可/拒否 |
| セッション永続化 | ★★☆☆☆ | 中 | 会話履歴のext3fs保存 |
| サブエージェント | ★★☆☆☆ | 高 | プロセスベースの並行実行 |
| MCP統合 | ★☆☆☆☆ | 非常に高 | MCPプロトコルスタック全体が必要 |

---

## 4. Sodexへの統合戦略: 詳細設計

### 4.1 推奨アプローチ: 独自エージェントループの実装

Agent SDKはClaude Code CLIのラッパーであり、そのコアは以下のループ:

```
1. ユーザーからのプロンプトを受信
2. Claude APIにメッセージ送信（SSEストリーミング）
3. レスポンスを解析
4. tool_useブロックがあれば:
   a. ツールを実行
   b. tool_resultを構築
   c. 2に戻る（会話にtool_resultを追加）
5. stop_reason == "end_turn"なら完了
```

**Sodexではこのループを直接Cで実装する。** Claude Code CLIをサブプロセスとして起動するのではなく、Claude Messages APIを直接叩く。

### 4.2 実装フェーズ

#### Phase D: ツール呼び出し対応（次の実装ステップ）

**目的**: Claudeからの`tool_use`リクエストに応答し、結果を返送する

**必要な変更**:

1. **`claude_adapter.c`の拡張**: tool_result JSON構築関数の追加

```c
/* tool_resultメッセージの構築 */
struct claude_tool_result {
    char tool_use_id[64];
    int  is_error;
    char content[CLAUDE_MAX_TEXT];
    int  content_len;
};

int claude_build_tool_result(
    struct json_writer *jw,
    const struct claude_tool_result *result
);
```

2. **ツール定義のAPI送信**: Claudeにどのツールが利用可能か伝える

```c
/* ツール定義（Claude API tools パラメータ用） */
struct claude_tool_def {
    const char *name;
    const char *description;
    const char *input_schema_json;  /* JSON Schema文字列 */
};

int claude_build_request_with_tools(
    struct json_writer *jw,
    const char *model,
    const struct claude_message *messages,
    int message_count,
    const struct claude_tool_def *tools,
    int tool_count,
    int max_tokens,
    int stream
);
```

3. **ツールディスパッチャ**: ツール名に基づいて適切なハンドラを呼び出す

```c
/* ツールハンドラの関数ポインタ */
typedef int (*tool_handler_fn)(
    const char *input_json,
    int input_len,
    struct claude_tool_result *result
);

struct tool_registry {
    const char *name;
    tool_handler_fn handler;
};

/* 組み込みツール */
PRIVATE struct tool_registry builtin_tools[] = {
    {"read_file",    tool_read_file},
    {"write_file",   tool_write_file},
    {"list_dir",     tool_list_dir},
    {"run_command",  tool_run_command},
    {"get_system_info", tool_get_system_info},
    {NULL, NULL}
};
```

#### Phase E: マルチターン会話

**目的**: 会話履歴を保持し、文脈を維持した対話を実現

```c
#define MAX_CONVERSATION_TURNS  32
#define MAX_MESSAGE_CONTENT     8192

struct conversation_turn {
    enum { ROLE_USER, ROLE_ASSISTANT } role;
    /* テキストまたはtool_use/tool_resultの配列 */
    struct claude_content_block blocks[CLAUDE_MAX_BLOCKS];
    int block_count;
};

struct conversation {
    struct conversation_turn turns[MAX_CONVERSATION_TURNS];
    int turn_count;
    char system_prompt[4096];
    struct claude_tool_def *tools;
    int tool_count;
};

/* 会話にターンを追加 */
int conversation_add_turn(
    struct conversation *conv,
    const struct conversation_turn *turn
);

/* 全会話履歴をJSON化してAPI送信 */
int conversation_build_request(
    struct conversation *conv,
    struct json_writer *jw,
    const char *model,
    int max_tokens,
    int stream
);
```

#### Phase F: エージェントループ

**目的**: 自律的にツールを実行し、タスクを完了するエージェント

```c
/* エージェントの設定 */
struct agent_config {
    const char *model;
    const char *system_prompt;
    struct claude_tool_def *tools;
    int tool_count;
    int max_turns;           /* 最大ターン数（SDKデフォルト: 20） */
    int max_tokens_per_turn; /* 各ターンの最大トークン */
};

/* エージェントの状態 */
struct agent_state {
    struct conversation conv;
    int current_turn;
    enum {
        AGENT_IDLE,
        AGENT_WAITING_API,
        AGENT_EXECUTING_TOOL,
        AGENT_COMPLETE,
        AGENT_ERROR
    } status;
};

/* エージェントループのメイン関数 */
int agent_run(
    struct agent_config *config,
    const char *initial_prompt,
    const char *api_key,
    char *final_response,
    int response_buf_size
);
```

エージェントループの擬似コード:
```
agent_run(config, prompt, api_key, response):
    conversation_init(&conv, config->system_prompt, config->tools)
    conversation_add_user_message(&conv, prompt)

    for turn = 0; turn < config->max_turns; turn++:
        // Claude APIに送信
        claude_send_conversation(&conv, api_key, &resp)

        // アシスタントのレスポンスを会話に追加
        conversation_add_assistant_turn(&conv, &resp)

        // stop_reasonを確認
        if resp.stop_reason == CLAUDE_STOP_END_TURN:
            // 完了 - テキスト応答を返す
            copy_text_response(&resp, response)
            return AGENT_SUCCESS

        if resp.stop_reason == CLAUDE_STOP_TOOL_USE:
            // ツール呼び出しを処理
            for each tool_use block in resp:
                tool_result = execute_tool(block.name, block.input)
                conversation_add_tool_result(&conv, &tool_result)
            continue  // 次のターン

        if resp.stop_reason == CLAUDE_STOP_MAX_TOKENS:
            return AGENT_ERROR_MAX_TOKENS

    return AGENT_ERROR_MAX_TURNS
```

#### Phase G: 高度な統合（長期）

フックシステム、権限管理、セッション永続化、ホスト連携:

```c
/* フックシステム */
typedef int (*hook_fn)(
    const char *event_name,
    const char *tool_name,
    const char *tool_input,
    struct hook_response *response
);

struct agent_hooks {
    hook_fn pre_tool_use;
    hook_fn post_tool_use;
    hook_fn on_error;
};

/* 権限モデル */
enum permission_mode {
    PERM_DEFAULT,           /* 都度確認 */
    PERM_ACCEPT_EDITS,      /* ファイル操作は自動承認 */
    PERM_BYPASS,            /* 全自動承認 */
};

/* セッション永続化 */
int agent_save_session(
    const struct agent_state *state,
    const char *session_file_path  /* ext3fs上 */
);

int agent_resume_session(
    struct agent_state *state,
    const char *session_file_path
);
```

### 4.3 Sodex固有のツール設計

Agent SDKのビルトインツール（Read/Write/Bash等）をSodexの機能にマッピング:

| SDKツール | Sodex実装 | 対応するシステム機能 |
|----------|----------|-------------------|
| `read_file` | ext3fs read | `ext3_read_file()` |
| `write_file` | ext3fs write | `ext3_write_file()` |
| `list_dir` | ext3fs readdir | `ext3_readdir()` |
| `run_command` | プロセス実行 | `execve()` + `waitpid()` |
| `get_system_info` | カーネル情報取得 | メモリ/プロセス/デバイス情報 |
| `network_request` | HTTP GET/POST | `http_get()` / `http_post()` |
| `read_memory` | メモリダンプ | カーネルメモリ読み取り |
| `manage_process` | プロセス管理 | `kill()` / `nice()` / `ps` |

**Sodex独自ツール（Agent SDKにないもの）**:

| ツール | 機能 | 用途 |
|-------|------|------|
| `read_serial` | RS232Cシリアル入力読み取り | 外部デバイスからの入力 |
| `write_vga` | VGA直接書き込み | 画面表示の制御 |
| `configure_pci` | PCIデバイス設定 | ハードウェア制御 |
| `allocate_memory` | カーネルメモリ確保 | 動的リソース管理 |
| `schedule_task` | タスクスケジューリング | プロセス優先度制御 |

---

## 5. Agent SDKプロトコル互換レイヤー（オプション案）

将来的にホスト側のAgent SDKとSodexカーネルを連携させる場合のアーキテクチャ:

```
┌────────────────────────┐     ┌──────────────────────────┐
│  ホスト（macOS/Linux）    │     │  Sodex カーネル (QEMU)     │
│                        │     │                          │
│  Agent SDK (Python)    │     │  agent_bridge.c          │
│  ├── query()           │────>│  ├── virtio-serial受信    │
│  ├── ToolExecutor      │     │  ├── JSONパース           │
│  └── SessionManager    │<────│  ├── ツール実行            │
│                        │     │  └── 結果JSON送信         │
│  virtio-serial (host)  │     │  virtio-serial (guest)    │
└────────────────────────┘     └──────────────────────────┘
```

**利点**:
- Claude Code CLIのフルパワーをSodexから利用可能
- Agent SDKのフック、MCP、サブエージェント機能が利用可能
- カーネル側はブリッジのみでシンプル

**欠点**:
- ホスト依存（自律OS哲学に反する）
- virtio-serialドライバの実装が必要
- レイテンシ増加

### 5.1 ブリッジプロトコル案

NDJSONベースでAgent SDKプロトコルのサブセットを採用:

```json
// ホスト → カーネル: ツール実行リクエスト
{"type":"tool_request","id":"tool_123","name":"read_file","input":{"path":"/etc/config"}}

// カーネル → ホスト: ツール実行結果
{"type":"tool_result","id":"tool_123","content":"ファイル内容...","is_error":false}

// カーネル → ホスト: エージェント起動リクエスト
{"type":"agent_request","prompt":"システムの状態を診断してください","tools":["read_file","get_system_info","list_dir"]}

// ホスト → カーネル: エージェント応答（ストリーミング）
{"type":"agent_stream","text":"システムの状態を確認します..."}
{"type":"agent_tool_use","name":"get_system_info","input":{}}
{"type":"agent_complete","text":"診断完了。メモリ使用率は正常です。"}
```

---

## 6. Agent SDKの設計から学ぶべきパターン

### 6.1 プロバイダ抽象化（既に実装済み）

Sodexの`llm_provider`は、Agent SDKのプロバイダシステムと同等の設計。拡張性を保っている。

### 6.2 ステップカウント制御

Agent SDKの`stopWhen: stepCountIs(N)`に相当する機能:

```c
/* エージェントの停止条件 */
enum stop_condition {
    STOP_END_TURN,         /* Claudeが自発的に停止 */
    STOP_MAX_TURNS,        /* 最大ターン数に到達 */
    STOP_SPECIFIC_TOOL,    /* 特定のツールが呼ばれた */
    STOP_ERROR,            /* エラー発生 */
};

struct stop_policy {
    int max_turns;
    const char *terminal_tool_name;  /* NULLなら無視 */
};
```

### 6.3 メッセージ型の設計

Agent SDKの5つのメッセージ型（System/Assistant/User/Result/Stream）は、Sodexの`claude_response`構造体を拡張する際のモデルとなる:

```c
enum agent_message_type {
    MSG_SYSTEM,      /* 初期化情報 */
    MSG_ASSISTANT,   /* Claudeの応答 */
    MSG_USER,        /* ユーザー入力/ツール結果 */
    MSG_RESULT,      /* 最終結果 */
    MSG_STREAM,      /* ストリーミング部分更新 */
};

struct agent_message {
    enum agent_message_type type;
    union {
        struct { char session_id[64]; } system;
        struct claude_response assistant;
        struct { struct claude_tool_result results[8]; int count; } user;
        struct {
            int is_error;
            int num_turns;
            int duration_ms;
            int input_tokens;
            int output_tokens;
        } result;
        struct { char text[256]; int text_len; } stream;
    };
};
```

### 6.4 ツール定義のJSON Schema

Claude API `tools`パラメータの形式:

```json
{
  "tools": [
    {
      "name": "read_file",
      "description": "Read the contents of a file at the given path",
      "input_schema": {
        "type": "object",
        "properties": {
          "path": {
            "type": "string",
            "description": "Absolute path to the file"
          }
        },
        "required": ["path"]
      }
    }
  ]
}
```

Sodexでのツール定義:
```c
/* JSON Schema文字列をコンパイル時に定義 */
#define TOOL_READ_FILE_SCHEMA \
    "{\"type\":\"object\"," \
    "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path\"}}," \
    "\"required\":[\"path\"]}"

PRIVATE struct claude_tool_def sodex_tools[] = {
    {
        .name = "read_file",
        .description = "Read the contents of a file on the Sodex filesystem",
        .input_schema_json = TOOL_READ_FILE_SCHEMA
    },
    /* ... */
};
```

---

## 7. 実装ロードマップ

### Phase D: ツール呼び出し対応（推定工数: 2-3週間）

| タスク | 説明 | 依存 |
|-------|------|------|
| D-1 | `claude_build_request_with_tools()` — ツール定義付きリクエスト構築 | なし |
| D-2 | `claude_build_tool_result()` — tool_result JSON構築 | なし |
| D-3 | ツールハンドラ実装: `read_file`, `list_dir` | ext3fs |
| D-4 | ツールハンドラ実装: `run_command` | プロセス管理 |
| D-5 | ツールディスパッチャ実装 | D-1, D-2, D-3 |
| D-6 | `tool_call`コマンド: 単発ツール呼び出しテスト | D-5 |
| D-7 | モックサーバーにtool_useレスポンス追加 | テスト用 |

### Phase E: マルチターン会話（推定工数: 1-2週間）

| タスク | 説明 | 依存 |
|-------|------|------|
| E-1 | `conversation`構造体と管理関数 | なし |
| E-2 | 会話履歴のJSON化 | E-1 |
| E-3 | `chat`コマンド: 対話型マルチターン | E-1, E-2 |
| E-4 | 会話の最大トークン管理 | E-2 |

### Phase F: エージェントループ（推定工数: 2-3週間）

| タスク | 説明 | 依存 |
|-------|------|------|
| F-1 | `agent_config` / `agent_state` 構造体定義 | Phase D, E |
| F-2 | `agent_run()` メインループ実装 | F-1 |
| F-3 | 停止条件の実装 | F-2 |
| F-4 | `agent`コマンド拡張: 自律エージェントモード | F-2 |
| F-5 | システムプロンプトの設計と調整 | F-4 |
| F-6 | エラーリカバリ（API失敗、ツールエラー） | F-2 |

### Phase G: 高度な機能（推定工数: 4-6週間）

| タスク | 説明 | 依存 |
|-------|------|------|
| G-1 | フックシステム | Phase F |
| G-2 | セッション永続化（ext3fs） | Phase F |
| G-3 | 権限管理 | G-1 |
| G-4 | （オプション）ホストブリッジ | virtio-serial |

---

## 8. リスクと制約

### 8.1 技術的リスク

| リスク | 影響 | 緩和策 |
|-------|------|--------|
| JSONバッファ溢れ | 長い会話でメモリ不足 | ターン数制限、古いターンの切り捨て |
| APIレイテンシ | ツール実行のたびにAPI往復 | ストリーミングで体感速度向上 |
| TLSメモリ使用量 | BearSSLのバッファ消費 | 接続プール、TLSセッション再利用 |
| トークン上限 | 長い会話がコンテキスト超過 | 自動コンパクション（要旨化） |
| 429レートリミット | 頻繁なAPI呼び出しで制限 | 既存リトライロジック活用 |

### 8.2 設計上の制約

- **外部ライブラリ不可**: すべてゼロから実装（Sodex原則）
- **メモリ制約**: i486アーキテクチャ、限られたRAM
- **ネットワーク依存**: Claude APIへのインターネット接続が必須
- **フリースタンディング環境**: `malloc`/`printf`等の標準関数なし

### 8.3 Agent SDKとの差分（意図的に実装しない機能）

| 機能 | 理由 |
|------|------|
| MCP統合 | オーバーヘッドが大きい。直接ツール実装で十分 |
| WebSearch/WebFetch | カーネルから直接ブラウザ操作は不要 |
| サブエージェント | プロセス管理のオーバーヘッド。シングルエージェントで十分 |
| セッション共有 | 単一カーネルインスタンス前提 |

---

## 9. 参考資料

### 公式ドキュメント
- [Agent SDK Overview](https://platform.claude.com/docs/en/agent-sdk/overview)
- [Agent SDK TypeScript Reference](https://platform.claude.com/docs/en/agent-sdk/typescript)
- [Agent SDK Python Reference](https://platform.claude.com/docs/en/agent-sdk/python)
- [Hooks Documentation](https://platform.claude.com/docs/en/agent-sdk/hooks)
- [Permissions Documentation](https://platform.claude.com/docs/en/agent-sdk/permissions)
- [Sessions Documentation](https://platform.claude.com/docs/en/agent-sdk/sessions)

### リポジトリ
- [claude-agent-sdk-typescript](https://github.com/anthropics/claude-agent-sdk-typescript)
- [claude-agent-sdk-python](https://github.com/anthropics/claude-agent-sdk-python)
- [claude-agent-sdk-demos](https://github.com/anthropics/claude-agent-sdk-demos)

### npm / PyPI
- [@anthropic-ai/claude-agent-sdk](https://www.npmjs.com/package/@anthropic-ai/claude-agent-sdk)
- [claude-agent-sdk (PyPI)](https://pypi.org/project/claude-agent-sdk/)

### Sodex関連ドキュメント
- `specs/agent-transport/plans/09-sse-parser.md` — SSEパーサ設計
- `specs/agent-transport/plans/10-claude-adapter.md` — Claudeアダプタ設計
- `specs/agent-transport/plans/11-claude-streaming-smoke.md` — ストリーミングスモークテスト
- `docs/research/autonomous_os_architecture_report.md` — 自律OS設計戦略
- `docs/research/sodex_agent_stateless_os_report.md` — ステートレスOS設計
