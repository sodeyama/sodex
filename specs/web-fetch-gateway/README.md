# Web Fetch Gateway Spec

GitHub Issue #20:
agent が最新 Web 情報を安定して扱うための、host 側構造化取得基盤と
guest 側 command / tool の導入計画。

現状の `websearch` は検索結果スニペットまで、`curl` は raw HTTP/HTTPS 取得までで、
本文抽出、JavaScript 前提ページ、引用付き要約、安全制御を一貫して扱えない。
この spec では、guest に汎用 HTML パーサを積む代わりに、host 側に richer runtime を置き、
agent へ構造化 JSON と bounded output を返す経路を整える。

## 背景

現状の制約:

- `websearch` は host 側 SearXNG の検索 JSON を整形表示するだけで、本文は取得しない
- `curl` は raw fetch 専用で、大きい HTML、抽出、JS rendering に弱い
- agent の built-in tool には Web 本文取得専用 tool がない
- tool result は bounded output 前提で、長大な HTML 全文を文脈へ渡す設計に向かない
- 最新情報のユースケースでは「検索候補は見つかるが、最終判断に必要な本文を安定取得できない」状態になりやすい

この構造では以下が難しい:

- 検索結果から本文取得までを、agent が narrow tool で安定して辿ること
- 静的 HTML と JS 前提ページを同じ設計で扱うこと
- allowlist、method 制限、size 制限を最初から入れること
- citation / metadata 付きで LLM に渡すこと
- live Web に強く依存しない smoke test を持つこと

## ゴール

- host 側に構造化 Web gateway を置き、`/fetch` で本文抽出済み JSON を返せる
- guest 側に `webfetch` command と `fetch_url` agent tool を追加できる
- `websearch` で候補発見、`webfetch` / `fetch_url` で本文取得、という役割分担を固定できる
- domain allowlist、`GET` / `HEAD` 制限、size / timeout 制限、`render_js` opt-in を持てる
- bounded output と artifact 連携で、大きいページでも agent 文脈を壊さず扱える
- 東京の天気のような「最新情報 + Web 参照」ユースケースを source URL 付きで処理できる

## 非ゴール

- guest 側 freestanding 環境に汎用 DOM/HTML パーサやブラウザを積むこと
- 初手から unrestricted internet browsing を許可すること
- ログイン必須サイト、複雑な認証フロー、cookie jar の実装
- CI で live site の安定性に依存すること
- `curl` をブラウザ相当に拡張してこの問題を直接解くこと

## 目標アーキテクチャ

```text
        ┌──────────────────────────────────────────┐
        │ QEMU guest                               │
        │  - websearch                             │
        │  - webfetch                              │
        │  - agent fetch_url tool                  │
        └─────────────────┬────────────────────────┘
                          │ HTTP/JSON
        ┌─────────────────┴────────────────────────┐
        │ host Web gateway                         │
        │  - static fetch                          │
        │  - optional JS render                    │
        │  - main content extract                  │
        │  - citation / metadata                   │
        │  - allowlist / timeout / size policy     │
        └──────────────┬───────────────────────────┘
                       │
        ┌──────────────┴───────────────┐
        │ host services                 │
        │  - existing SearXNG (/search) │
        │  - new fetch API (/fetch)     │
        └───────────────────────────────┘
```

基本フロー:

1. `websearch` が候補 URL を見つける
2. `webfetch` / `fetch_url` が host gateway に URL を渡す
3. host gateway が本文抽出済み JSON を返す
4. guest 側は要約表示または tool result 化する

## 実装ポリシー

- raw HTML ではなく抽出済み JSON を返す
- host 側で HTML 取得、抽出、必要なら JS rendering を行う
- 初手は static HTML を優先し、JS rendering は opt-in にする
- policy は deny-all を基本とし、allowlist で開ける
- bounded output を前提に、artifact path を返せる構造にする
- CI は mock page / mock weather endpoint を優先し、live Web は手動デモ扱いにする

## Plans

| # | ファイル | 概要 | 依存 | この plan の出口 |
|---|---------|------|------|-------------------|
| 01 | [01-host-structured-web-gateway.md](plans/01-host-structured-web-gateway.md) | host 側 `/fetch` API と抽出済みレスポンス契約を定義し、static HTML 取得の最小実装を作る | なし | host gateway が URL から `title` / `excerpt` / `main_text` / `metadata` を返せる |
| 02 | [02-guest-webfetch-command-and-tool.md](plans/02-guest-webfetch-command-and-tool.md) | guest 側 `webfetch` command と `fetch_url` tool を追加し、agent が narrow tool として使えるようにする | 01 | `websearch` → `webfetch` / `fetch_url` の導線が guest 上で成立する |
| 03 | [03-safety-and-policy-controls.md](plans/03-safety-and-policy-controls.md) | allowlist、method 制限、size / timeout、`render_js` opt-in、抽出 sanitization を固める | 01, 02 | Web gateway が安全制御込みで運用できる |
| 04 | [04-validation-and-weather-smoke.md](plans/04-validation-and-weather-smoke.md) | host/QEMU smoke、mock weather、artifact、運用手順を固める | 02, 03 | 代表ユースケースと失敗系が継続検証できる |

## マイルストーン

| マイルストーン | 対象 plan | 到達状態 |
|---------------|-----------|----------|
| M0: 構造化 fetch の成立 | 01 | host 側で URL から抽出済み JSON を返せる |
| M1: guest 統合 | 02 | `webfetch` command と `fetch_url` tool が使える |
| M2: 安全制御の固定 | 03 | allowlist と bounded output を前提に運用できる |
| M3: ユースケース検証 | 04 | weather を含む代表シナリオを smoke で確認できる |

## 現在の到達点

- host 側 `src/tools/web_fetch_gateway.py` が `/fetch` と `/healthz` を提供する
- guest 側 `webfetch` command と agent `fetch_url` tool が動く
- allowlist、`GET` / `HEAD` 制限、size / timeout、content-type 制限、`render_js` opt-in が入っている
- host unit test、QEMU `webfetch` smoke、agent integration weather scenario が通る
- README と `/etc/CLAUDE.md` / `system_prompt.txt` に使い分けを反映済み

## 実装順序

1. host 側 `/fetch` の JSON 契約と static HTML 取得を固める
2. guest 側 `webfetch` と `fetch_url` を追加し、agent 導線を作る
3. allowlist、timeout、`render_js`、sanitization を追加する
4. mock/live を分けた検証基盤と運用手順を整える

## 並行化の考え方

- `01` の API 契約と fixture は先に固められる
- `02` の guest client と command/tool は `01` の schema が決まれば並行可能
- `03` の policy は `01` 実装に寄せて同時進行できるが、最終固定は `02` 後
- `04` は mock fixture を先に作り、QEMU smoke は `02`, `03` 後にまとめる

## 主な設計判断

- host 側サービスは既存 `websearch` と同じく HTTP/JSON proxy として置く
- 初手は `searxng` と分離した fetch gateway にする
  - search と fetch で trust boundary と責務が違うため
- 初手の host 実装は Python を優先する
  - repo の host tool / smoke に馴染み、static fetch と fixture test を小さく始めやすいため
- JS rendering は phase 1 では必須にしない
  - static fetch で十分なページを先に安定化させる
- CI は mock page / mock weather endpoint を使う
  - live Web 依存を減らすため

## 未解決論点

- fetch gateway を `searxng` 前段に置くか、独立サービスにするか
- `render_js` を Playwright 系にするか、別コンテナに分離するか
- `main_text` の抽出 heuristic を Python 標準ライブラリ寄りにするか、host 専用依存を認めるか
- citation を「URL 1 本単位」にするか、「抜粋単位」にするか
- weather のような高価値ユースケースを generic fetch の上に載せるか、専用 tool を別で切るか

## 関連 spec / 参考

- `specs/agent-transport/README.md`
- `specs/server-runtime/README.md`
- `searxng/README.md`
- `src/rootfs-overlay/etc/CLAUDE.md`
- `src/rootfs-overlay/etc/agent/system_prompt.txt`
