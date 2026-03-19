# Plan 01: host 側構造化 Web gateway

## 概要

現状の `websearch` は search JSON、`curl` は raw fetch までで止まっている。
agent が最新 Web 情報を使うには、URL から title / excerpt / main_text / metadata を
構造化して返す host 側 gateway が必要になる。

この plan では static HTML を対象に、guest から叩ける `/fetch` API と
その最小実装を定義する。

## 依存と出口

- 依存: なし
- この plan の出口
  - host 側 `/fetch` API の JSON 契約が固定される
  - static HTML を取得し、抽出済みレスポンスを返せる
  - redirect, content-type, truncation, metadata が揃う

## 現状

- `searxng/` は search JSON を返すが、本文取得責務は持っていない
- guest 側 `curl` は raw body を出すだけで、抽出済み本文へ整形しない
- host 側に汎用 Web 本文 gateway は存在しない

## 方針

- search と fetch を分ける
  - `websearch` は候補発見
  - `/fetch` は本文取得と抽出
- 初手は static HTML を対象にする
- 返却は raw HTML ではなく抽出済み JSON にする
- host 実装は Python を優先し、repo の host tool / smoke に寄せる

## API 案

### Request

```json
{
  "url": "https://example.com/article",
  "render_js": false,
  "extract_mode": "article",
  "max_bytes": 262144,
  "max_chars": 4000
}
```

### Response

```json
{
  "url": "https://example.com/article",
  "final_url": "https://example.com/article",
  "status": 200,
  "content_type": "text/html",
  "title": "Example",
  "excerpt": "短い要約",
  "main_text": "抽出済み本文",
  "links": [
    {"href": "https://example.com/about", "text": "About"}
  ],
  "fetched_at": "2026-03-19T00:00:00Z",
  "source_hash": "sha256:...",
  "truncated": false
}
```

## 設計判断

- `POST /fetch` にする
  - `render_js`, `extract_mode`, `max_*` を素直に増やせるため
- response は本文中心にする
  - HTML 全文は artifact や debug 用に回し、既定レスポンスには入れない
- redirect は `final_url` に反映する
- `links` は全部ではなく、上限件数つきの抜粋に留める

## 実装ステップ

1. `/fetch` request / response schema を定義する
2. host 側 fetch server の雛形を追加する
3. static HTML 取得、redirect、content-type 判定を実装する
4. title / excerpt / main_text / links の抽出を実装する
5. truncation と metadata を加える

## 変更対象

- 新規候補
  - `src/tools/web_fetch_gateway.py`
  - `src/tools/web_fetch_extract.py`
  - `src/tools/web_fetch_fixtures/`
- 既存候補
  - `README.md`
  - `searxng/README.md`
  - `src/rootfs-overlay/etc/CLAUDE.md`

## 検証

- 静的 HTML fixture から期待する `title` / `main_text` を返せる
- redirect を含む URL で `final_url` が正しい
- `text/html` 以外の content-type でも安全に失敗または限定取得できる
- 抽出結果が `max_chars` で切られる

## 完了条件

- guest 側が使うための `/fetch` JSON 契約が固まっている
- static HTML の代表ページを抽出済み JSON で返せる
- 次の guest command / tool plan がこの API に乗って進められる

