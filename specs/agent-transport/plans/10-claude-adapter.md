# Plan 10: Claude API アダプタ

## 概要

レポート § 11.4 のパターン 2「Adapter 関数によるリクエスト/レスポンスの分離」に従い、
Claude Messages API 固有のリクエスト生成・レスポンス解析を Adapter 層に閉じ込める。
Core 層（HTTP, JSON, SSE, TLS）は Claude 非依存に保つ。

## 目標

- Claude Messages API のリクエストボディを生成できる
- SSE ストリーミング応答を逐次的にパースして内部表現に変換できる
- `tool_use` ブロックを検出して内部表現に格納できる（実行は後続 EPIC）
- `stop_reason` による制御フロー判定ができる
- API バージョンやヘッダの変更が Config 層の 1–2 行修正で済む

## 設計

### レイヤー構成（レポート § 11.3 準拠）

```
Config Layer:
  claude_endpoint  — host, path, port
  claude_headers[] — x-api-key, anthropic-version, content-type

Adapter Layer:
  claude_build_request()     — 内部表現 → Claude JSON
  claude_parse_sse_event()   — SSE data JSON → 内部表現
  claude_parse_response()    — 非ストリーミング JSON → 内部表現

Core Layer (触らない):
  http_client, json, sse_parser, tls_client
```

### Config 構造体

```c
/* src/include/agent/api_config.h */

struct api_endpoint {
    const char *host;
    const char *path;
    u_int16_t   port;
};

struct api_header {
    const char *name;
    const char *value;
};

/* 設定変更時はここだけ修正 */
PRIVATE const struct api_endpoint claude_endpoint = {
    .host = "api.anthropic.com",
    .path = "/v1/messages",
    .port = 443,
};

PRIVATE const struct api_header claude_headers[] = {
    { "content-type",      "application/json" },
    { "x-api-key",         NULL },          /* 実行時に secret store から設定 */
    { "anthropic-version", "2023-06-01" },
    { NULL, NULL }
};
```

### 内部表現

```c
/* src/include/agent/claude_adapter.h */

#define CLAUDE_MAX_TOOL_NAME   64
#define CLAUDE_MAX_TOOL_INPUT 4096
#define CLAUDE_MAX_TEXT       8192
#define CLAUDE_MAX_TOOLS        8   /* 1 レスポンス内の tool_use 数上限 */

enum claude_content_type {
    CLAUDE_CONTENT_TEXT,
    CLAUDE_CONTENT_TOOL_USE,
};

struct claude_tool_use {
    char id[64];                           /* "toolu_01A09q90..." */
    char name[CLAUDE_MAX_TOOL_NAME];       /* "read_file" */
    char input_json[CLAUDE_MAX_TOOL_INPUT]; /* 生 JSON 文字列 */
    int  input_json_len;
};

struct claude_content_block {
    enum claude_content_type type;
    union {
        struct {
            char text[CLAUDE_MAX_TEXT];
            int  text_len;
        } text;
        struct claude_tool_use tool_use;
    };
};

enum claude_stop_reason {
    CLAUDE_STOP_NONE = 0,
    CLAUDE_STOP_END_TURN,
    CLAUDE_STOP_TOOL_USE,
    CLAUDE_STOP_MAX_TOKENS,
    CLAUDE_STOP_ERROR,
};

struct claude_response {
    char id[64];                           /* メッセージ ID */
    char model[64];                        /* 使用モデル名 */
    enum claude_stop_reason stop_reason;
    struct claude_content_block blocks[8]; /* content ブロック配列 */
    int block_count;
    int input_tokens;
    int output_tokens;
};

/* メッセージ（会話履歴） */
struct claude_message {
    const char *role;                      /* "user", "assistant" */
    const char *content;                   /* テキスト（簡易版） */
    /* tool_result の場合は別構造体で拡張 */
};
```

### Adapter 関数

```c
/* リクエスト生成 */
int claude_build_request(
    struct json_writer *jw,
    const char *model,
    const struct claude_message *msgs, int msg_count,
    const char *system_prompt,           /* NULL = なし */
    int max_tokens,
    int stream                           /* 1 = SSE */
);

/* 非ストリーミングレスポンス解析 */
int claude_parse_response(
    const char *json_str, int json_len,
    struct claude_response *out
);

/* SSE イベント 1 件の解析（sse_parser が返す data を処理） */
int claude_parse_sse_event(
    const struct sse_event *event,
    struct claude_response *state        /* 累積的に構築 */
);

/* stop_reason の判定 */
int claude_needs_tool_call(const struct claude_response *resp);
/* → stop_reason == CLAUDE_STOP_TOOL_USE なら true */

/* tool_result メッセージの構築（後続 EPIC で MCP 結果を返す際に使用） */
int claude_build_tool_result(
    struct json_writer *jw,
    const char *tool_use_id,
    const char *result_json, int result_json_len,
    int is_error
);
```

### SSE 処理フロー

```
recv loop:
  http_recv_chunk()
    → sse_feed(chunk)
      → SSE_EVENT_DATA
        → claude_parse_sse_event(event, &response)
          → event_name == "content_block_delta"
            → response.blocks[i].text に追記
          → event_name == "content_block_start", type == "tool_use"
            → response.blocks[i].tool_use に ID/name 記録
          → event_name == "message_delta"
            → response.stop_reason 更新
          → event_name == "message_stop"
            → ループ終了
```

### エラー処理

| HTTP ステータス | 対応 |
|---------------|------|
| 200 | 正常 → SSE パースに進む |
| 400 | リクエスト不正 → エラーボディをパースしてログ |
| 401 | API キー不正 → ログ + 停止 |
| 429 | レート制限 → Retry-After 待ち → リトライ |
| 500, 529 | サーバエラー → 指数バックオフリトライ |

リトライ:
```c
#define CLAUDE_MAX_RETRIES     3
#define CLAUDE_INITIAL_WAIT_MS 1000
#define CLAUDE_MAX_WAIT_MS    30000
```

### LLM プロバイダ抽象化（準備）

レポート § 11.4 パターン 4 に従い、将来の LLM 切り替えに備える:

```c
/* src/include/agent/llm_provider.h */

struct llm_provider {
    const char *name;
    const struct api_endpoint *endpoint;
    const struct api_header *headers;
    int (*build_request)(struct json_writer *jw, ...);
    int (*parse_response)(const char *json, int len, struct claude_response *out);
    int (*parse_sse_event)(const struct sse_event *ev, struct claude_response *state);
};

/* 現段階では Claude のみ登録 */
extern const struct llm_provider provider_claude;
```

Agent Loop（後続 EPIC）はこの `struct llm_provider` 経由で呼ぶため、
LLM 差し替え時に Agent Loop 本体は変更不要。

## 実装ステップ

1. `api_config.h` に Config 構造体を定義する
2. `claude_adapter.h` に内部表現とAPI を定義する
3. `claude_build_request()` を実装する（JSON ライター使用）
4. `claude_parse_response()` を実装する（非ストリーミング、テスト用）
5. `claude_parse_sse_event()` を実装する（ストリーミング）
6. `claude_needs_tool_call()` と `claude_build_tool_result()` を実装する
7. `llm_provider.h` のインターフェースを定義し、`provider_claude` を登録する
8. エラー処理（429 リトライ、5xx バックオフ）を実装する

## テスト

### host 単体テスト (`tests/test_claude_adapter.c`)

リクエスト生成:
- 単純なテキストメッセージ → 正しい JSON
- system prompt 付き → `system` フィールドあり
- stream=true → `"stream": true`
- ツール定義付き → `tools` 配列

レスポンスパース（非ストリーミング）:
- テキストのみ → `blocks[0].type == TEXT`, `stop_reason == END_TURN`
- tool_use → `blocks[1].type == TOOL_USE`, `stop_reason == TOOL_USE`
- エラー応答 → `stop_reason == ERROR`

SSE パース:
- fixture の SSE ストリームを feed → 最終的な response 構造体が正しい
- tool_use の ID, name, input が正しく取得できる

### fixture (`tests/fixtures/claude/`)

- `request_simple.json` — 期待されるリクエストボディ
- `request_with_tools.json` — ツール定義付きリクエスト
- `response_text.json` — テキストのみ非ストリーミング応答
- `response_tool_use.json` — tool_use 非ストリーミング応答
- `sse_text_stream.txt` — テキストのみ SSE ストリーム
- `sse_tool_use_stream.txt` — tool_use SSE ストリーム
- `sse_error_stream.txt` — エラー SSE ストリーム
- `error_429.json` — レート制限エラーボディ

### QEMU スモーク

- Plan 11 で実施する

## 変更対象

- 新規:
  - `src/agent/claude_adapter.c`
  - `src/include/agent/claude_adapter.h`
  - `src/include/agent/api_config.h`
  - `src/include/agent/llm_provider.h`
  - `tests/test_claude_adapter.c`
  - `tests/fixtures/claude/`
- 既存:
  - `src/makefile` — agent サブディレクトリ追加

## 完了条件

- Claude API リクエストを JSON ライターで正しく生成できる
- SSE ストリームから tool_use を含む応答を正しくパースできる
- API バージョンやヘッダの変更が Config 層の修正で完結する
- LLM プロバイダ抽象化のインターフェースが定義されている
- host 単体テストが通る

## 依存と後続

- 依存: Plan 03 (JSON), Plan 08 (TLS), Plan 09 (SSE)
- 後続: Plan 11 (結合テスト), EPIC-05 (MCP), EPIC-06 (Agent Loop)

---

## 技術調査結果

### A. Anthropic Messages API 完全仕様

#### エンドポイント

`POST https://api.anthropic.com/v1/messages`

#### 認証ヘッダ

```
x-api-key: $ANTHROPIC_API_KEY
anthropic-version: 2023-06-01
content-type: application/json
```

#### リクエストボディ — 必須フィールド

| フィールド | 型 | 制約 |
|---|---|---|
| `model` | string | `claude-opus-4-6`, `claude-sonnet-4-6`, `claude-haiku-4-5` 等 |
| `max_tokens` | number | モデル依存の上限 |
| `messages` | array | 最大100,000メッセージ、user/assistant 交互 |

#### オプションフィールド

| フィールド | 型 | デフォルト |
|---|---|---|
| `system` | string \| array | なし |
| `temperature` | number | 1.0 (0.0–1.0) |
| `top_p` | number | なし |
| `top_k` | number | なし |
| `stop_sequences` | array of string | なし |
| `stream` | boolean | false |
| `tools` | array | なし |
| `tool_choice` | object | `"auto"` |
| `metadata` | object | なし |

### B. レスポンス構造（非ストリーミング）

```json
{
  "id": "msg_01A1B2C3D4E5F6G7H8I9J0K1L",
  "type": "message",
  "role": "assistant",
  "content": [
    {"type": "text", "text": "Hello!"}
  ],
  "model": "claude-sonnet-4-6",
  "stop_reason": "end_turn",
  "stop_sequence": null,
  "usage": {
    "input_tokens": 25,
    "output_tokens": 15,
    "cache_creation_input_tokens": 0,
    "cache_read_input_tokens": 0
  }
}
```

`stop_reason`: `"end_turn"`, `"max_tokens"`, `"stop_sequence"`, `"tool_use"`

### C. SSE ストリーミングイベントの完全な JSON 構造

**message_start**:
```json
{"type":"message_start","message":{"id":"msg_...","type":"message","role":"assistant","content":[],"model":"...","stop_reason":null,"usage":{"input_tokens":25,"output_tokens":1}}}
```

**content_block_start (text)**:
```json
{"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}
```

**content_block_start (tool_use)**:
```json
{"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_...","name":"get_weather","input":{}}}
```

**content_block_delta (text_delta)**:
```json
{"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}}
```

**content_block_delta (input_json_delta)**:
```json
{"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"location\":\"San Francisco\"}"}}
```

**message_delta**:
```json
{"type":"message_delta","delta":{"stop_reason":"end_turn","stop_sequence":null},"usage":{"output_tokens":15}}
```

**ping**: `{"type":"ping"}`
**error**: `{"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}}`

### D. tool_use / tool_result の会話フロー

#### Step 1: Claude が tool_use を返す

```json
{"type":"tool_use","id":"toolu_01A09q90qw90lq917835lq9","name":"get_weather","input":{"location":"San Francisco, CA"}}
```

#### Step 2: ツール結果を返す (user message)

```json
{
  "role": "user",
  "content": [
    {
      "type": "tool_result",
      "tool_use_id": "toolu_01A09q90qw90lq917835lq9",
      "content": "15 degrees"
    }
  ]
}
```

is_error フラグ: `"is_error": true` の場合、content は空にできない（400エラー）

#### tool_choice オプション

- `{"type":"auto"}` — Claude が判断（デフォルト）
- `{"type":"any"}` — 何らかのツールを必ず使用
- `{"type":"tool","name":"get_weather"}` — 指定ツールを必ず使用
- `{"type":"none"}` — ツール使用禁止

### E. 429/5xx エラーのリトライ戦略

#### HTTP エラーコード

| コード | type | リトライ |
|---|---|---|
| 400 | `invalid_request_error` | 不可 |
| 401 | `authentication_error` | 不可 |
| 429 | `rate_limit_error` | `retry-after` 秒待ち |
| 500 | `api_error` | 指数バックオフ |
| 529 | `overloaded_error` | 指数バックオフ |

#### レート制限ヘッダ

| ヘッダ | 説明 |
|---|---|
| `retry-after` | リトライまでの秒数 |
| `anthropic-ratelimit-requests-limit` | RPM 上限 |
| `anthropic-ratelimit-requests-remaining` | 残りリクエスト数 |

#### 推奨パラメータ

初期待機 1秒、倍率 2x、最大待機 60秒、最大リトライ 5回

### 参考資料

- [Anthropic Messages API](https://platform.claude.com/docs/en/api/messages)
- [Anthropic Streaming Messages](https://platform.claude.com/docs/en/api/messages-streaming)
- [Anthropic Tool Use](https://platform.claude.com/docs/en/docs/build-with-claude/tool-use)
- [Anthropic Rate Limits](https://platform.claude.com/docs/en/api/rate-limits)
- [Anthropic Errors](https://platform.claude.com/docs/en/api/errors)
