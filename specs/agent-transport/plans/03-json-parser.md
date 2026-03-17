# Plan 03: JSON パーサ / ライター

## 概要

Claude API のリクエスト/レスポンスと MCP の JSON-RPC はすべて JSON。
フリースタンディング環境で動く最小限の JSON パーサとライターを実装する。

## 目標

- JSON の 6 型（object, array, string, number, bool, null）をパースできる
- ネスト構造を扱える（Claude の `content[].type`, MCP の `result.tools[]`）
- JSON を構造体から生成できる（リクエストボディ生成用）
- malloc なしで動作する（caller 提供の固定バッファ上で動作）

## 設計方針

### パーサ: トークナイザ + ツリービルダの 2 層

**Layer 1: トークナイザ**
入力文字列を順にスキャンし、トークン（`{`, `}`, `[`, `]`, `:`, `,`, string, number, bool, null）を返す。

**Layer 2: ツリービルダ**
トークン列から固定サイズノード配列にツリーを構築する。
jsmn (Jasmine) 方式を参考に、ノードをフラット配列で管理する。

### データ構造

```c
/* src/include/json.h */

enum json_type {
    JSON_NONE = 0,
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_STRING,
    JSON_NUMBER,
    JSON_BOOL,
    JSON_NULL
};

/* パース済みトークン。入力バッファ上の [start, end) を参照する */
struct json_token {
    enum json_type type;
    int start;          /* 入力バッファ内の開始位置 */
    int end;            /* 入力バッファ内の終了位置 */
    int size;           /* 子要素数 (object: キー数, array: 要素数) */
    int parent;         /* 親トークンのインデックス (-1 = ルート) */
};

/* パーサ状態 */
struct json_parser {
    int pos;            /* 入力の現在位置 */
    int toknext;        /* 次のトークンスロット */
    int toksuper;       /* 現在の親トークン */
    int error;          /* エラーコード */
};

#define JSON_MAX_TOKENS  256   /* 1 パース当たりの最大トークン数 */
```

### アクセサ API

パース済みトークン配列に対するヘルパー:

```c
/* オブジェクトからキーで値トークンを検索 */
int json_find_key(
    const char *json_str,
    const struct json_token *tokens, int token_count,
    int obj_token,              /* 検索対象の object トークンインデックス */
    const char *key             /* キー名 */
);
/* 戻り値: 値トークンのインデックス、見つからなければ -1 */

/* 配列の i 番目の要素トークンを取得 */
int json_array_get(
    const struct json_token *tokens, int token_count,
    int array_token,
    int index
);

/* トークンの文字列値を NUL 終端バッファにコピー */
int json_token_str(
    const char *json_str,
    const struct json_token *tok,
    char *out, int out_cap
);

/* トークンの数値を int に変換 */
int json_token_int(
    const char *json_str,
    const struct json_token *tok,
    int *out
);

/* トークンの bool 値を取得 */
int json_token_bool(
    const char *json_str,
    const struct json_token *tok,
    int *out
);
```

### JSON ライター

SSH の `ssh_writer` パターンを踏襲する。バッファ + 位置 + エラーフラグ。

```c
struct json_writer {
    char *buf;
    int   cap;
    int   len;
    int   error;        /* バッファ超過で 1 にセット */
    int   need_comma;   /* 次の値の前にカンマが必要か */
    int   depth;        /* ネスト深さ（デバッグ用） */
};

void jw_init(struct json_writer *jw, char *buf, int cap);
void jw_object_start(struct json_writer *jw);
void jw_object_end(struct json_writer *jw);
void jw_array_start(struct json_writer *jw);
void jw_array_end(struct json_writer *jw);
void jw_key(struct json_writer *jw, const char *key);
void jw_string(struct json_writer *jw, const char *value);
void jw_string_n(struct json_writer *jw, const char *value, int len);
void jw_int(struct json_writer *jw, int value);
void jw_bool(struct json_writer *jw, int value);
void jw_null(struct json_writer *jw);
void jw_raw(struct json_writer *jw, const char *raw, int len);  /* プリフォーマット済み JSON 埋め込み */
int  jw_finish(struct json_writer *jw);  /* NUL 終端。エラー時 -1 */
```

使用例（Claude API リクエスト生成）:
```c
char buf[4096];
struct json_writer jw;
jw_init(&jw, buf, sizeof(buf));
jw_object_start(&jw);
  jw_key(&jw, "model");
  jw_string(&jw, "claude-sonnet-4-20250514");
  jw_key(&jw, "max_tokens");
  jw_int(&jw, 1024);
  jw_key(&jw, "messages");
  jw_array_start(&jw);
    jw_object_start(&jw);
      jw_key(&jw, "role");
      jw_string(&jw, "user");
      jw_key(&jw, "content");
      jw_string(&jw, "Hello");
    jw_object_end(&jw);
  jw_array_end(&jw);
jw_object_end(&jw);
jw_finish(&jw);
/* buf = {"model":"claude-sonnet-4-20250514","max_tokens":1024,"messages":[{"role":"user","content":"Hello"}]} */
```

### エスケープ処理

- パーサ: `\"`, `\\`, `\/`, `\n`, `\r`, `\t`, `\uXXXX` をデコード
- ライター: 制御文字と `"`, `\` をエスケープ。`\uXXXX` は ASCII 範囲外のみ
- `\uXXXX` のサロゲートペアは初期段階では非対応（Claude API のレスポンスで実用上は出ない）

### エラーコード

```c
#define JSON_OK                  0
#define JSON_ERR_NOMEM          (-1)  /* トークン配列が足りない */
#define JSON_ERR_INVALID        (-2)  /* 不正な JSON */
#define JSON_ERR_PARTIAL        (-3)  /* 入力が途中で切れている */
#define JSON_ERR_KEY_NOT_FOUND  (-4)  /* キーが見つからない */
#define JSON_ERR_TYPE_MISMATCH  (-5)  /* 型が期待と違う */
#define JSON_ERR_BUF_OVERFLOW   (-6)  /* ライターのバッファ超過 */
```

## 実装ステップ

1. `json.h` にデータ構造、エラーコード、API を定義する
2. トークナイザを実装する（文字列スキャン、数値スキャン、エスケープ処理）
3. ツリービルダを実装する（`json_parse()` がトークン配列を埋める）
4. アクセサ（`json_find_key`, `json_array_get`, `json_token_str`, `json_token_int`）を実装する
5. JSON ライターを実装する（`jw_*` 系関数）
6. host 単体テストを書く

## テスト

### host 単体テスト (`tests/test_json_parser.c`)

パーサ:
- 空オブジェクト `{}`
- 空配列 `[]`
- ネストした object/array
- 文字列のエスケープ（`\"`, `\\`, `\n`, `\uXXXX`）
- 数値（正、負、小数点は非対応を確認）
- bool と null
- `json_find_key()` でキー検索
- `json_array_get()` で配列アクセス
- Claude API レスポンスのサンプル JSON をパースして `content[0].type`, `content[0].text` を取得

ライター:
- 空オブジェクト
- ネスト構造
- 文字列エスケープ
- Claude API リクエストの生成
- バッファ溢れ時のエラー

### fixture (`tests/fixtures/json/`)

- `claude_response_text.json` — text のみの応答
- `claude_response_tool_use.json` — tool_use を含む応答
- `claude_response_sse_delta.json` — SSE の content_block_delta
- `mcp_tools_list.json` — MCP ツール一覧
- `mcp_tool_result.json` — MCP ツール実行結果
- `invalid_json.txt` — 不正な JSON 各種

## 変更対象

- 新規:
  - `src/lib/json.c`
  - `src/include/json.h`
  - `tests/test_json_parser.c`
  - `tests/fixtures/json/`

## 完了条件

- Claude API / MCP で使う構造の JSON をパースしてフィールドアクセスできる
- JSON ライターで Claude API リクエストボディを生成できる
- malloc なしで固定バッファ上で動作する
- host 単体テストが通る

## 依存と後続

- 依存: なし（独立して実装可能）
- 後続: Plan 04 (結合テスト), Plan 09 (SSE パーサ内の JSON デルタ), Plan 10 (Claude アダプタ)

---

## 技術調査結果

### A. jsmn (Jasmine) JSON パーサの設計

#### 設計思想

「JSON の完全な正しさチェックや一時オブジェクトの動的確保は多くの場合やりすぎ」という哲学。約200行の C89 コード、API 関数はたった2つ (`jsmn_init`, `jsmn_parse`)、動的メモリ確保ゼロ、シングルパスのインクリメンタルパーシング。

#### フラット配列でのツリー表現

`{"name":"Jack","age":27}` のパース結果:

| index | type | start | end | size | 意味 |
|-------|------|-------|-----|------|------|
| 0 | OBJECT | 0 | 24 | 2 | ルートオブジェクト(ペア数=2) |
| 1 | STRING | 2 | 6 | 1 | キー "name" (値を1つ持つ) |
| 2 | STRING | 9 | 13 | 0 | 値 "Jack" |
| 3 | STRING | 15 | 18 | 1 | キー "age" |
| 4 | PRIMITIVE | 20 | 22 | 0 | 値 27 |

**size フィールドの意味**:
- OBJECT: 直接の子のキーバリュー**ペア数**（×2ではない）
- ARRAY: 直接の要素数
- STRING（キーとして）: size=1（対応する値が1つ）
- STRING/PRIMITIVE（値として）: size=0

**JSMN_PARENT_LINKS**: 有効にすると各トークンに `parent` フィールドが追加され、O(1) で親へ遡れる。無効の場合、閉じ括弧時にトークン配列を逆走査して未閉鎖コンテナを探す。

#### malloc なしの動作原理

```c
jsmntok_t tokens[128];  /* スタック上に確保 */
jsmn_parser parser;
jsmn_init(&parser);
int r = jsmn_parse(&parser, json, json_len, tokens, 128);
```

2パス手法: `tokens=NULL` で第1パスを行い必要トークン数を取得可能。

#### Sodex 実装への示唆

JSMN_PARENT_LINKS を有効にして実装するのが望ましい（トークンあたり20バイト = 5×int）。Claude API レスポンスの場合 128–256 トークンで十分。

### B. JSON 仕様詳細 (RFC 8259)

#### 文字列エスケープシーケンス全種類

| シーケンス | 文字 | コードポイント |
|-----------|------|---------------|
| `\"` | 引用符 | U+0022 |
| `\\` | バックスラッシュ | U+005C |
| `\/` | スラッシュ | U+002F |
| `\b` | バックスペース | U+0008 |
| `\f` | フォームフィード | U+000C |
| `\n` | 改行 | U+000A |
| `\r` | キャリッジリターン | U+000D |
| `\t` | タブ | U+0009 |
| `\uXXXX` | Unicode 文字 | U+XXXX |

制御文字 (U+0000–U+001F) は必ずエスケープが必要。

#### サロゲートペア

BMP 外の文字 (U+10000 以上) は UTF-16 サロゲートペアで表現:
- 高サロゲート: U+D800–U+DBFF、低サロゲート: U+DC00–U+DFFF
- 変換: `code_point = 0x10000 + (high - 0xD800) * 0x400 + (low - 0xDC00)`
- Claude API レスポンスに含まれる可能性は低い。jsmn 方式ならエスケープをデコードせず start/end 範囲で参照するため問題を回避できる。

#### 数値フォーマット

```
number = [ minus ] int [ frac ] [ exp ]
```

- 先頭ゼロ禁止: `012` は不正
- 小数部は1桁以上必須: `1.` は不正
- Claude API の `max_tokens` や `usage` は常に整数。レスポンスパースでは整数のみ扱えれば十分

#### ネスト深さ

RFC 8259 は具体的制限なし。Claude API レスポンスの実際のネスト深さ:
- テキスト応答: 3–4 階層
- tool_use: 4–5 階層
- Sodex では **最大深さ 16** で十分安全

### C. フリースタンディング環境での JSON 実装パターン

#### Sodex の既存基盤

- **文字列操作** (src/lib/string.c): `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strchr`, `strrchr`, `memcpy`, `memset`, `memmove`, `memcmp`
- **数値→文字列変換** (src/vga.c の `_kvsnprintf`): `%d`, `%x`, `%s`, `%c` サポート
- **メモリ管理**: `kalloc`/`kfree` (memory.h), `memb` 固定ブロックアロケータ (src/lib/memb.c)
- **型定義** (sys/types.h): `u_int8_t`, `u_int16_t`, `u_int32_t`, `int32_t`, `size_t`
- **可変長引数** (stdarg.h): `va_list`, `va_start`, `va_arg`, `va_end`

#### 固定バッファ JSON 生成パターン

```c
typedef struct {
    char *buf;
    int   capacity;
    int   pos;
    int   overflow;  /* バッファ超過フラグ */
} json_writer_t;

static void jw_putc(json_writer_t *w, char c) {
    if (w->pos < w->capacity - 1)
        w->buf[w->pos++] = c;
    else
        w->overflow = 1;
}
```

全書き込みを `jw_putc` 経由に統一し、overflow フラグで超過を検出。

#### 整数→文字列変換

Sodex の `_kvsnprintf` 内の `%d` 処理がそのまま再利用可能。逆順生成→反転のパターン。

### D. Claude API の JSON 構造

#### リクエストボディ（必須フィールド）

```json
{
  "model": "claude-sonnet-4-20250514",
  "max_tokens": 1024,
  "messages": [{"role": "user", "content": "Hello"}]
}
```

オプション: `system`, `temperature`, `stream`, `tools`, `tool_choice`, `stop_sequences`, `metadata`

#### レスポンスボディ

```json
{
  "id": "msg_013Zva2CMHLNjK56",
  "type": "message",
  "role": "assistant",
  "content": [{"type": "text", "text": "応答テキスト"}],
  "model": "claude-sonnet-4-20250514",
  "stop_reason": "end_turn",
  "stop_sequence": null,
  "usage": {"input_tokens": 10, "output_tokens": 20}
}
```

`stop_reason` の値: `"end_turn"`, `"max_tokens"`, `"stop_sequence"`, `"tool_use"`

#### tool_use コンテンツブロック

```json
{
  "type": "tool_use",
  "id": "toolu_01D7FLrfh4GYq7yT1ULFeyMV",
  "name": "function_name",
  "input": {"param1": "value1"}
}
```

#### SSE ストリーミングの content_block_delta (input_json_delta)

```json
{"type": "content_block_delta", "index": 1, "delta": {"type": "input_json_delta", "partial_json": "{\"location\":\"San Fra"}}
```

`input_json_delta` は部分的な JSON 文字列。`content_block_stop` 受信後に連結してパースする。

### 参考資料

- [jsmn (Jasmine) - GitHub](https://github.com/zserge/jsmn)
- [RFC 8259 - The JavaScript Object Notation (JSON) Data Interchange Format](https://www.rfc-editor.org/rfc/rfc8259)
- [Anthropic Messages API](https://platform.claude.com/docs/en/api/messages)
