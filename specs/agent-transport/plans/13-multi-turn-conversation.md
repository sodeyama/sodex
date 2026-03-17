# Plan 13: マルチターン会話

## 概要

Claude API との複数ターンにわたる会話履歴を管理し、tool_result の返送と
文脈を維持した対話を実現する。Claude Agent SDK の「セッション管理」と
「ステートフルクライアント」に相当する機能をユーザランドで実装する。

## 目標

- 会話履歴（user/assistant の交互メッセージ + tool_use/tool_result）を保持する
- 全履歴を含むリクエストを Claude API に送信し、文脈を維持した応答を得る
- tool_use → ツール実行 → tool_result → 再問い合わせの 1 サイクルを実現する
- 会話のトークン使用量を追跡し、上限に近づいたら警告する
- `chat` コマンドとして対話型インターフェースを提供する

## 背景

Plan 11 の `claude_send_message()` は 1 ターンのみ。Plan 12 でツール実行が可能になる。
本 Plan でこれらを統合し、以下のフローを実現する:

```
User prompt → Claude API → text応答 → 表示して完了
                         → tool_use → ツール実行 → tool_result → Claude API → ...
```

Claude Agent SDK では `ClaudeSDKClient` がセッション内で会話を保持する。
Sodex ではメモリ上の配列で会話履歴を管理する。

## 設計

### 会話データ構造

```c
/* src/usr/include/agent/conversation.h */

#define CONV_MAX_TURNS          32
#define CONV_MAX_CONTENT      8192
#define CONV_MAX_BLOCKS          8
#define CONV_SYSTEM_PROMPT_MAX 4096

/* 会話ターンの役割 */
enum conv_role {
    CONV_ROLE_USER,
    CONV_ROLE_ASSISTANT,
};

/* 会話ターンのコンテンツブロック */
struct conv_block {
    enum {
        CONV_BLOCK_TEXT,
        CONV_BLOCK_TOOL_USE,
        CONV_BLOCK_TOOL_RESULT,
    } type;
    union {
        struct {
            char text[CONV_MAX_CONTENT];
            int  text_len;
        } text;
        struct claude_tool_use tool_use;      /* Plan 10 の構造体を再利用 */
        struct {
            char tool_use_id[64];
            char content[CONV_MAX_CONTENT];
            int  content_len;
            int  is_error;
        } tool_result;
    };
};

/* 1 つの会話ターン */
struct conv_turn {
    enum conv_role role;
    struct conv_block blocks[CONV_MAX_BLOCKS];
    int block_count;
};

/* 会話全体 */
struct conversation {
    char system_prompt[CONV_SYSTEM_PROMPT_MAX];
    int  system_prompt_len;

    struct conv_turn turns[CONV_MAX_TURNS];
    int turn_count;

    /* トークン追跡 */
    int total_input_tokens;
    int total_output_tokens;
};
```

### 会話管理 API

```c
/* 初期化 */
void conv_init(struct conversation *conv, const char *system_prompt);

/* ユーザーメッセージの追加 */
int conv_add_user_text(struct conversation *conv, const char *text);

/* アシスタント応答の追加（claude_response から変換） */
int conv_add_assistant_response(
    struct conversation *conv,
    const struct claude_response *resp
);

/* tool_result の追加（user ロールで） */
int conv_add_tool_results(
    struct conversation *conv,
    const struct claude_tool_result *results,
    int result_count
);

/* 会話履歴を JSON に変換（API 送信用） */
int conv_build_messages_json(
    const struct conversation *conv,
    struct json_writer *jw
);

/* 会話のリセット（system_prompt は保持） */
void conv_reset(struct conversation *conv);

/* ターン数と容量チェック */
int conv_is_full(const struct conversation *conv);
int conv_token_count(const struct conversation *conv);
```

### 会話 JSON 化のフロー

```
conv_build_messages_json():
  jw_array_begin(jw);  // "messages": [

  for each turn:
    jw_object_begin(jw);
    jw_key_string(jw, "role", turn->role == USER ? "user" : "assistant");
    jw_key(jw, "content");

    if turn has single text block:
      jw_string(jw, turn->blocks[0].text.text);  // 文字列として直接

    else:
      jw_array_begin(jw);  // content: [
      for each block:
        if CONV_BLOCK_TEXT:
          {"type":"text","text":"..."}
        if CONV_BLOCK_TOOL_USE:
          {"type":"tool_use","id":"...","name":"...","input":{...}}
        if CONV_BLOCK_TOOL_RESULT:
          {"type":"tool_result","tool_use_id":"...","content":"...","is_error":false}
      jw_array_end(jw);

    jw_object_end(jw);

  jw_array_end(jw);
```

### 会話付き API 送信

```c
/* src/usr/include/agent/claude_client.h — 拡張 */

/* 会話全体を送信してストリーミング応答を受信 */
int claude_send_conversation(
    const struct llm_provider *provider,
    struct conversation *conv,
    const char *api_key,
    struct claude_response *out
);
```

内部フロー:
```
claude_send_conversation():
  1. json_writer で全体リクエスト構築:
     - model, max_tokens, stream
     - system (conv->system_prompt)
     - tools (tool_registry から列挙)
     - messages (conv_build_messages_json)
  2. claude_client の HTTP+SSE 送信ループを呼ぶ
  3. レスポンスの usage でトークン追跡を更新
  4. conv_add_assistant_response() で会話に追加
```

### chat コマンド

```c
/* src/usr/command/chat.c */

/*
 * 使い方: chat [system_prompt]
 *
 * 対話ループ:
 *   > ユーザー入力
 *   Claude: テキスト応答（または tool_use → 自動実行 → 再問い合わせ）
 *   > 次の入力
 *   ...
 *   > exit  (終了)
 */
```

### トークン管理

```c
#define CONV_TOKEN_WARNING_THRESHOLD  150000  /* 警告閾値 */
#define CONV_TOKEN_MAX                190000  /* 強制停止閾値 */

/* トークン超過時の対策 */
enum conv_token_action {
    CONV_TOKEN_OK,           /* 問題なし */
    CONV_TOKEN_WARNING,      /* 警告表示 */
    CONV_TOKEN_TRUNCATE,     /* 古いターンを切り捨て */
    CONV_TOKEN_STOP,         /* 会話を強制停止 */
};

conv_token_action conv_check_tokens(const struct conversation *conv);
```

トークン超過時は最古のターン（system_prompt 直後）から切り捨てる。
Claude Agent SDK の「コンパクション」に相当するが、要旨化ではなく単純切り捨て。

## 実装ステップ

1. `conversation.h` に会話データ構造と API を定義する
2. `conversation.c` に `conv_init`, `conv_add_*`, `conv_reset` を実装する
3. `conv_build_messages_json()` を実装する — 全ターンの JSON 化
4. tool_result ブロックの JSON 化を実装する（複数 tool_result 対応）
5. `claude_send_conversation()` を `claude_client.c` に追加する
6. トークン追跡と閾値チェックを実装する
7. 古いターン切り捨てロジックを実装する
8. `chat` コマンドを実装する — 対話ループ + readline 的入力
9. host 単体テストを書く
10. モックサーバに tool_use → tool_result → 再応答のシナリオを追加する
11. QEMU で 1 サイクル（prompt → tool_use → tool_result → final_response）を確認する

## テスト

### host 単体テスト (`tests/test_conversation.c`)

- `conv_add_user_text()` → `conv_build_messages_json()` → 正しい JSON
- user → assistant → user → assistant の 4 ターン → 正しい交互メッセージ
- tool_use 応答の追加 → content 配列に tool_use ブロックが含まれる
- tool_result の追加 → user ロールで tool_result ブロックが含まれる
- 混在: text + tool_use → tool_result → text の完全フロー
- `conv_is_full()` → MAX_TURNS で true
- `conv_check_tokens()` → 閾値超過で正しいアクション

### fixture (`tests/fixtures/conversation/`)

- `two_turn_simple.json` — 2 ターンの単純会話
- `tool_use_cycle.json` — tool_use → tool_result → 再応答の完全サイクル
- `multi_tool_results.json` — 1 ターンに複数 tool_result
- `full_request_body.json` — 期待される完全なリクエストボディ

### QEMU スモーク

- モックサーバに 2 段階シナリオを実装:
  1. 初回: tool_use を返す
  2. tool_result 受信後: テキスト応答を返す
- Sodex 側で自動的に tool_result を返送し、最終応答を受信

## 変更対象

- 新規:
  - `src/usr/include/agent/conversation.h`
  - `src/usr/lib/libagent/conversation.c`
  - `src/usr/command/chat.c`
  - `tests/test_conversation.c`
  - `tests/fixtures/conversation/`
- 既存:
  - `src/usr/lib/libagent/claude_client.c` — `claude_send_conversation()` 追加
  - `src/usr/include/agent/claude_client.h` — 新 API 宣言
  - `tests/mock_claude_server.py` — マルチターンシナリオ追加
  - `src/usr/command/makefile` — chat コマンド追加

## 完了条件

- 4 ターン以上の会話を正しい JSON で API に送信できる
- tool_use → tool_result → 再応答の 1 サイクルが自動で回る
- トークン追跡が正しく加算される
- `chat` コマンドで対話的にマルチターン会話ができる
- host 単体テストが通る

## 依存と後続

- 依存: Plan 10 (Claude adapter), Plan 12 (ツール実行)
- 後続: Plan 14 (エージェントループ)
