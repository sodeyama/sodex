# Plan 09: SSE パーサ

## 概要

Claude API のストリーミング応答は Server-Sent Events (SSE) 形式で返る。
HTTP レスポンスボディを行単位でパースし、イベントを再構成するパーサを実装する。

## 目標

- `Content-Type: text/event-stream` のレスポンスを逐次的にパースできる
- TCP 受信の断片化（1 イベントが複数 recv に分かれる）に対応する
- `event:`, `data:`, 空行のプロトコルを正しく処理する
- Claude API の主要イベントタイプを認識する

## SSE プロトコル概要 (W3C 仕様)

```
event: message_start\n
data: {"type":"message_start","message":{...}}\n
\n
event: content_block_delta\n
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}}\n
\n
event: message_stop\n
data: {"type":"message_stop"}\n
\n
```

ルール:
- 行は `\n` で区切られる
- `event: ` プレフィックスでイベント名
- `data: ` プレフィックスでデータ（複数行可、改行で結合）
- 空行でイベント区切り（dispatch）
- `:` で始まる行はコメント（無視）

## 設計

### データ構造

```c
/* src/include/sse_parser.h */

#define SSE_MAX_EVENT_NAME   64
#define SSE_MAX_DATA_LEN   8192   /* 1 イベントの data 最大長 */

enum sse_event_type {
    SSE_EVENT_NONE = 0,
    SSE_EVENT_DATA,           /* イベント完了（data が揃った） */
    SSE_EVENT_NEED_MORE,      /* 入力が途中。追加データが必要 */
    SSE_EVENT_ERROR,          /* パースエラー */
    SSE_EVENT_DONE,           /* ストリーム終了（接続切断） */
};

struct sse_event {
    char event_name[SSE_MAX_EVENT_NAME];   /* "message_start" 等 */
    char data[SSE_MAX_DATA_LEN];           /* data フィールドの結合値 */
    int  data_len;
};

struct sse_parser {
    /* 未処理の入力断片を保持する行バッファ */
    char line_buf[SSE_MAX_DATA_LEN];
    int  line_len;

    /* 構築中のイベント */
    struct sse_event pending;
    int has_event_name;
    int has_data;
};
```

### API

```c
/* 初期化 */
void sse_parser_init(struct sse_parser *p);

/* 受信データを投入し、完了したイベントを取り出す。
   chunk = 今回 recv したデータ、chunk_len = そのバイト数。
   out に完了イベントを格納し、SSE_EVENT_DATA を返す。
   まだ完了していなければ SSE_EVENT_NEED_MORE を返す。
   1 回の feed で複数イベントが完了する可能性があるため、
   SSE_EVENT_DATA が返る限り繰り返し呼ぶ。 */
int sse_feed(
    struct sse_parser *p,
    const char *chunk, int chunk_len,
    struct sse_event *out
);

/* 残りの入力オフセットを取得（feed が消費した位置） */
int sse_consumed(const struct sse_parser *p);

/* パーサをリセット */
void sse_parser_reset(struct sse_parser *p);
```

### 断片受信への対応

TCP は任意の位置でデータを分割する。例:

```
recv 1: "event: content_block_del"
recv 2: "ta\ndata: {\"type\":\"content_"
recv 3: "block_delta\",\"delta\":{\"text\":\"Hello\"}}\n\n"
```

対策:
1. `line_buf` に未処理データを蓄積する
2. `\n` が見つかるまで蓄積を継続する
3. 完全な行が得られたら処理する
4. `data:` 行が複数あればスペース区切りで結合する
5. 空行でイベントを dispatch する

### Claude API のイベント型

| event 名 | 用途 | data 内の key |
|-----------|------|--------------|
| `message_start` | メッセージ開始 | `message.id`, `message.model` |
| `content_block_start` | ブロック開始 | `content_block.type` ("text" or "tool_use") |
| `content_block_delta` | テキスト差分 | `delta.text` |
| `content_block_stop` | ブロック終了 | `index` |
| `message_delta` | メッセージ差分 | `delta.stop_reason` |
| `message_stop` | メッセージ終了 | — |
| `error` | エラー | `error.type`, `error.message` |
| `ping` | キープアライブ | — |

## 実装ステップ

1. `sse_parser.h` にデータ構造と API を定義する
2. `sse_feed()` の行バッファリングロジックを実装する
3. `event:` と `data:` プレフィックスの認識を実装する
4. 空行によるイベント dispatch を実装する
5. コメント行（`:` 始まり）の読み飛ばしを実装する
6. host 単体テストを書く

## テスト

### host 単体テスト (`tests/test_sse_parser.c`)

- 完全な 1 イベント → SSE_EVENT_DATA
- 複数イベントの連続入力
- 行の途中で分割された入力 → NEED_MORE → 残りを feed → DATA
- `data:` が複数行 → 結合
- コメント行のスキップ
- 空の data → 空文字列イベント
- バッファ溢れ → SSE_EVENT_ERROR

### fixture (`tests/fixtures/sse/`)

- `stream_text_only.txt` — テキストのみの応答
- `stream_tool_use.txt` — tool_use を含む応答
- `stream_fragmented.txt` — 意図的に行の途中で分割したデータ
- `stream_with_ping.txt` — ping イベント入りの応答
- `stream_error.txt` — error イベントを含む応答

### HTTP クライアントとの結合

Plan 02 の HTTP クライアントで `Content-Type: text/event-stream` を検出した場合、
Content-Length ベースの受信ではなく `recv → sse_feed → イベント処理` のループに切り替える。
この結合は Plan 10 (Claude アダプタ) で行う。

## 変更対象

- 新規:
  - `src/net/sse_parser.c`
  - `src/include/sse_parser.h`
  - `tests/test_sse_parser.c`
  - `tests/fixtures/sse/`

## 完了条件

- Claude API の全主要イベントタイプをパースできる
- 断片受信に対して正しくバッファリングしてイベントを再構成できる
- host 単体テストが通る
- HTTP クライアントと組み合わせ可能な API になっている

## 依存と後続

- 依存: なし（独立して実装可能。JSON パーサは SSE 内の data を処理する際に使うが、SSE パーサ自体は JSON 非依存）
- 後続: Plan 10 (Claude アダプタで SSE + JSON を組み合わせる)

---

## 技術調査結果

### A. Server-Sent Events 仕様 (WHATWG HTML Living Standard Section 9.2)

#### 行終端の扱い

3種類の行終端を認識する:

| 終端 | バイト列 |
|------|---------|
| CRLF | `0x0D 0x0A` |
| LF | `0x0A` (前に CR なし) |
| CR | `0x0D` (後に LF なし) |

CR を受信した時点ではまだ行終端か判断できない。次のバイトが LF なら CRLF として1つの終端、LF 以外なら CR 単独の終端。

#### 行処理アルゴリズム

```
行を受信するたびに:
1. 空行 → イベントをディスパッチ
2. ':' で始まる → コメント (無視、キープアライブ用)
3. ':' を含む → 最初の ':' の前 = フィールド名、後 = 値 (先頭スペース1つ除去)
4. ':' なし → 行全体 = フィールド名、値は空文字列

フィールド処理 (case-sensitive、リテラル比較):
  "event" → event_type = 値
  "data"  → data_buf += 値 + '\n'  ★複数 data 行は LF で結合
  "id"    → NULL 文字がなければ last_event_id = 値
  "retry" → 全て ASCII 数字なら retry_ms = atoi(値)
  その他  → 無視

ディスパッチ:
  - data_buf が空なら何もせずリセット
  - data_buf の末尾 LF を1つ除去
  - event_type が空なら "message"
  - イベントを発火
  - data_buf と event_type をリセット (last_event_id は保持)
```

### B. Claude API SSE ストリーミングの実際のフォーマット

#### 完全なイベントシーケンス (実測)

```sse
event: message_start
data: {"type":"message_start","message":{"id":"msg_...","type":"message","role":"assistant","content":[],"model":"...","stop_reason":null,"usage":{"input_tokens":25,"output_tokens":1}}}

event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}

event: ping
data: {"type":"ping"}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: message_delta
data: {"type":"message_delta","delta":{"stop_reason":"end_turn","stop_sequence":null},"usage":{"output_tokens":15}}

event: message_stop
data: {"type":"message_stop"}
```

#### tool_use のストリーミング

```sse
event: content_block_start
data: {"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_...","name":"get_weather","input":{}}}

event: content_block_delta
data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"location\":"}}

event: content_block_delta
data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":" \"San Francisco\""}}

event: content_block_delta
data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"}"}}

event: content_block_stop
data: {"type":"content_block_stop","index":1}
```

`input_json_delta` は部分的な JSON 文字列。`content_block_stop` 後に連結してパース。

#### 最小限の処理対象イベント

1. `content_block_delta` の `text_delta` — テキスト応答の本体
2. `message_stop` — ストリーム終了検知
3. `error` — エラーハンドリング
4. `ping` — 無視

### C. TCP 受信断片化への対処パターン

#### 推奨: リニアバッファ + memmove 方式

i486 のメモリ制約と実装の単純さを考慮:

```c
#define SSE_BUF_SIZE  4096

void sse_feed(sse_stream_t *s, const char *recv_buf, int recv_len) {
    /* バッファに追記 */
    int copy = min(recv_len, SSE_BUF_SIZE - s->pos);
    memcpy(s->buf + s->pos, recv_buf, copy);
    s->pos += copy;

    /* 完全な行を探して処理 */
    int start = 0;
    for (int i = 0; i < s->pos; i++) {
        if (s->buf[i] == '\n') {
            int line_len = i - start;
            if (line_len > 0 && s->buf[start + line_len - 1] == '\r')
                line_len--;
            sse_process_line(s, s->buf + start, line_len);
            start = i + 1;
        }
    }

    /* 未処理データを先頭に memmove */
    if (start > 0) {
        int remaining = s->pos - start;
        memmove(s->buf, s->buf + start, remaining);
        s->pos = remaining;
    }
}
```

#### バッファサイズの考慮

- `content_block_delta`: 通常 50–200 バイト
- `message_start`: 最大約 500 バイト
- `input_json_delta` 蓄積: 数 KB
- **4KB のラインバッファで十分**

### 参考資料

- [WHATWG HTML Living Standard - Server-sent events](https://html.spec.whatwg.org/multipage/server-sent-events.html)
- [Anthropic Streaming Messages](https://platform.claude.com/docs/en/api/messages-streaming)
- [MDN - Using server-sent events](https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events)
