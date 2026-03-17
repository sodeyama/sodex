# Plan 02: HTTP/1.1 クライアント

## 概要

能動 TCP 接続の上に、HTTP/1.1 リクエストの生成とレスポンスの解析を載せる。
既存の `http_server.c` にはサーバ側の HTTP パースがあるが、
クライアント側（リクエスト送信・レスポンス受信）は存在しない。

## 目標

- HTTP/1.1 リクエスト（GET, POST）を構造体からバイト列に生成できる
- レスポンスのステータス行・ヘッダ・ボディを解析できる
- Content-Length ベースのボディ受信が完了判定できる
- ヘッダをテーブル駆動で追加でき、Claude/MCP adapter が共有できる API にする

## 設計

### データ構造

```c
/* src/include/http_client.h */

#define HTTP_MAX_HEADERS    16
#define HTTP_MAX_HEADER_LEN 256
#define HTTP_MAX_URL_LEN    256

struct http_header {
    const char *name;
    const char *value;
};

struct http_request {
    const char *method;                   /* "GET", "POST" */
    const char *host;                     /* "api.anthropic.com" */
    const char *path;                     /* "/v1/messages" */
    u_int16_t   port;                     /* 443 */
    const struct http_header *headers;    /* NULL 終端配列 */
    const char *body;                     /* NULL = ボディなし */
    int         body_len;
};

struct http_response {
    int  status_code;                     /* 200, 429, 500 等 */
    char status_text[64];                 /* "OK", "Too Many Requests" */
    char content_type[128];               /* "application/json" */
    int  content_length;                  /* -1 = 未指定 */
    int  is_chunked;                      /* Transfer-Encoding: chunked */
    int  is_sse;                          /* Content-Type: text/event-stream */
    const char *body;                     /* ボディ開始位置へのポインタ */
    int  body_len;                        /* 受信済みボディ長 */
};
```

### リクエスト生成

テーブル駆動でヘッダを組み立てる。レポート § 11.4 のパターン 1 に従う。

```c
int http_build_request(
    char *buf, int cap,
    const struct http_request *req
);
```

出力例:
```
POST /v1/messages HTTP/1.1\r\n
Host: api.anthropic.com\r\n
Content-Type: application/json\r\n
Content-Length: 42\r\n
x-api-key: sk-ant-...\r\n
\r\n
{"model":"claude-sonnet-4-20250514",...}
```

### レスポンスパース

段階的にパースする:

1. **ステータス行**: `HTTP/1.1 200 OK\r\n` → `status_code=200`
2. **ヘッダ**: 行単位で `name: value` を抽出。`Content-Length`, `Content-Type`, `Transfer-Encoding` を認識
3. **ヘッダ終端**: `\r\n\r\n` 検出でボディ開始
4. **ボディ受信**: Content-Length 分を受け取るまでループ

```c
/* ステータス行 + ヘッダを解析。ヘッダ終端までのバイト数を返す */
int http_parse_response_headers(
    const char *buf, int len,
    struct http_response *resp
);

/* ボディが Content-Length 分揃ったか判定 */
int http_body_complete(
    const struct http_response *resp,
    int received_body_len
);
```

### エラーコード

```c
#define HTTP_OK                0
#define HTTP_ERR_BUF_OVERFLOW (-1)   /* 出力バッファ不足 */
#define HTTP_ERR_PARSE_STATUS (-2)   /* ステータス行パース失敗 */
#define HTTP_ERR_PARSE_HEADER (-3)   /* ヘッダパース失敗 */
#define HTTP_ERR_NO_CONTENT_LENGTH (-4)   /* Content-Length 未指定 */
#define HTTP_ERR_BODY_TOO_LARGE   (-5)   /* ボディがバッファ超過 */
```

### 制限事項（明示的に非対応）

- **chunked Transfer-Encoding**: Phase A では非対応。SSE 対応時 (Plan 09) に拡張する
- **HTTP/2**: 対象外
- **リダイレクト (3xx)**: 初期段階では非対応
- **Keep-Alive**: 初期は 1 リクエスト 1 接続で close する

## 実装ステップ

1. `http_client.h` にデータ構造とエラーコードを定義する
2. `http_build_request()` を実装し、method/host/path/headers/body からリクエスト文字列を生成する
3. `http_parse_response_headers()` を実装し、ステータス行とヘッダを解析する
4. `http_body_complete()` を実装する
5. 高レベル関数 `http_do_request()` を実装する:
   - `kern_connect()` → `kern_send(request)` → ヘッダ受信ループ → ボディ受信ループ → `kern_close_socket()`
6. 既存 `http_server.c` のパース関数との重複を確認し、共通化できるものは `http_common.h` に抽出する

## テスト

### host 単体テスト (`tests/test_http_client.c`)

- リクエスト生成:
  - GET with no body
  - POST with JSON body
  - 複数ヘッダの出力順序
  - バッファ溢れ時のエラー
- レスポンスパース:
  - `200 OK` + Content-Length + ボディ
  - `429 Too Many Requests` + Retry-After ヘッダ
  - `500 Internal Server Error`
  - ヘッダなしの不正応答
  - Content-Length 不一致

### fixture (`tests/fixtures/http/`)

- `response_200_json.txt` — 正常 JSON 応答
- `response_429.txt` — レート制限応答
- `response_500.txt` — サーバエラー
- `response_no_content_length.txt` — Content-Length なし
- `response_partial.txt` — 途中で切れたヘッダ

### QEMU スモーク

- Plan 04 でモックサーバとの結合テストとして実施する

## 変更対象

- 新規:
  - `src/net/http_client.c`
  - `src/include/http_client.h`
  - `tests/test_http_client.c`
  - `tests/fixtures/http/`
- 既存:
  - `src/makefile` (net ビルド対象に追加)

## 完了条件

- `http_build_request()` が正しい HTTP/1.1 リクエストを生成する
- `http_parse_response_headers()` がステータス、Content-Length、Content-Type を解析する
- Claude/MCP adapter から共有できる API になっている（ヘッダがテーブル駆動）
- host 単体テストが通る

## 依存と後続

- 依存: Plan 01 (能動 TCP)
- 後続: Plan 04 (平文結合テスト), Plan 08 (TLS クライアント), Plan 09 (SSE パーサ)

---

## 技術調査結果

### A. HTTP/1.1 リクエストフォーマット (RFC 7230/7231)

#### リクエスト行の構文

```
request-line = method SP request-target SP HTTP-version CRLF
```

- `SP` = 0x20（空白1文字のみ。タブや複数空白は不可）
- `CRLF` = 0x0D 0x0A

#### 必須ヘッダ

`Host` ヘッダは HTTP/1.1 で唯一の必須ヘッダ:
```
Host: api.anthropic.com\r\n
```

ヘッダ行の形式:
```
header-field = field-name ":" OWS field-value OWS CRLF
```
- `OWS` = Optional WhiteSpace。コロン後の空白は省略可能だが慣例として1個入れる
- **field-name とコロンの間に空白を入れてはならない**

#### ヘッダ終端

```
HTTP-message = start-line
               *( header-field CRLF )
               CRLF              ← 空行（ヘッダとボディの境界）
               [ message-body ]
```

ヘッダ部の終端は `\r\n\r\n`（最後のヘッダの CRLF + 空行の CRLF）。

#### 完全なリクエスト例

```
POST /v1/messages HTTP/1.1\r\n
Host: api.anthropic.com\r\n
Content-Type: application/json\r\n
Content-Length: 128\r\n
x-api-key: sk-ant-xxx\r\n
anthropic-version: 2023-06-01\r\n
\r\n
{"model":"claude-sonnet-4-20250514","max_tokens":1024,"messages":[{"role":"user","content":"Hello"}]}
```

### B. HTTP/1.1 レスポンスパースの詳細

#### ステータス行

```
status-line = HTTP-version SP status-code SP reason-phrase CRLF
```

例: `HTTP/1.1 200 OK\r\n`

パース手順:
1. `HTTP/1.1` を確認
2. SP 後の 3 桁数字をステータスコードとして取得
3. reason-phrase は無視可能

#### ヘッダの case-insensitive 比較

RFC 7230 で明記: field-name は case-insensitive。`Content-Length` = `content-length` = `CONTENT-LENGTH`。

Sodex に `strncasecmp` 相当がないため、簡易実装が必要:
```c
/* 各バイトを小文字化して比較。英字のみ変換する実装が安全 */
```

#### Content-Length によるボディ受信完了判定

1. ヘッダから `Content-Length` の値を取得（整数にパース）
2. ヘッダ終端 `\r\n\r\n` の後から、その長さ分のバイトを受信すれば完了
3. TCP は分割到着するため、累積受信バイト数を管理する必要がある

#### Transfer-Encoding: chunked

```
chunk = chunk-size [ chunk-ext ] CRLF chunk-data CRLF
last-chunk = 1*"0" [ chunk-ext ] CRLF
```

デコード: 16進数の chunk-size を読む → 0 なら終了 → size 分のデータを読む → CRLF を消費 → 繰り返し

**重要**: Content-Length と Transfer-Encoding が両方存在する場合、Transfer-Encoding が優先。

#### バッファサイズの注意

- `SOCK_RXBUF_SIZE` = 4096バイト (socket.h)。Claude API レスポンスは数 KB になりうるので拡張またはストリーミング処理が必要
- `SOCK_TXBUF_SIZE` = 1460バイト (MSS)。リクエストが大きい場合は分割送信が必要

### C. 組み込み環境での HTTP クライアント実装パターン

#### picohttpparser 方式（参考設計）

約1000行の C コード。動的メモリ割り当てゼロ。入力バッファへのポインタと長さのペアで結果を返す（ゼロコピー）。

#### Sodex 向け推奨: 独自実装

picohttpparser の設計思想を参考に、最小 HTTP レスポンスパーサを独自実装するのが最適:

- **外部ライブラリ禁止**の制約
- クライアント用途なのでレスポンスパースのみ
- 必要な機能が限定的: ステータスコード抽出、Content-Length 取得、ヘッダ終端検出、ボディ抽出

```c
struct http_header {
    const char *name;    /* recv_buf 内へのポインタ */
    int name_len;
    const char *value;
    int value_len;
};
```

#### 追加で必要な関数

- `strncasecmp` 相当（ヘッダ名比較用）
- `atoi` 相当（Content-Length 数値変換用）
- hex 文字列→整数変換（chunked の chunk-size パース用）
- `strstr` 相当（`\r\n\r\n` 検索用）

### D. Claude API (Anthropic Messages API) の HTTP 仕様

#### 必須 HTTP ヘッダ

| ヘッダ | 値 | 説明 |
|---|---|---|
| `Host` | `api.anthropic.com` | HTTP/1.1 必須 |
| `Content-Type` | `application/json` | 必須 |
| `x-api-key` | `sk-ant-...` | API キー |
| `anthropic-version` | `2023-06-01` | API バージョン |
| `Content-Length` | (数値) | ボディ長 |

#### エラーレスポンス

```json
{
  "type": "error",
  "error": {
    "type": "authentication_error",
    "message": "invalid x-api-key"
  }
}
```

エラータイプ: `invalid_request_error`, `authentication_error`, `permission_error`, `not_found_error`, `rate_limit_error`, `api_error`

#### レート制限ヘッダ

レスポンスに含まれる:
- `x-ratelimit-limit-requests` / `x-ratelimit-remaining-requests`
- `x-ratelimit-limit-tokens` / `x-ratelimit-remaining-tokens`

### 参考資料

- [RFC 7230 - HTTP/1.1 Message Syntax and Routing](https://www.rfc-editor.org/rfc/rfc7230)
- [RFC 7231 - HTTP/1.1 Semantics and Content](https://www.rfc-editor.org/rfc/rfc7231)
- [picohttpparser - GitHub](https://github.com/h2o/picohttpparser)
- [Anthropic Messages API](https://platform.claude.com/docs/en/api/messages)
