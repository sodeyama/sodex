# Web Fetch Gateway Tasks

`specs/web-fetch-gateway/README.md` を着手単位に落としたタスクリスト。
まず host 側の構造化 fetch を成立させ、その後に guest command / agent tool、
安全制御、検証の順で固める。

## 優先順

1. host 側 `/fetch` API と抽出済み JSON 契約
2. guest 側 `webfetch` command と `fetch_url` tool
3. allowlist / timeout / size / `render_js` 制御
4. mock weather を含む smoke と運用手順

## M0: host 側構造化 fetch

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | WFG-01 | `/fetch` の request / response schema、既定 host/port/path、環境変数名を定義する | なし | guest から叩く JSON 契約と設定名が固定される |
| [x] | WFG-02 | host 側 fetch gateway の最小実装を追加し、static HTML を取得して JSON を返す | WFG-01 | redirect 後の `final_url`, `status`, `content_type` を返せる |
| [x] | WFG-03 | title / excerpt / main_text / links の抽出 heuristic を実装する | WFG-02 | HTML から本文候補を JSON 化できる |
| [x] | WFG-04 | truncation と source metadata を実装する | WFG-03 | `truncated`, `fetched_at`, `source_hash` が返る |

## M1: guest 統合

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | WFG-05 | guest 側の shared fetch client を実装する | WFG-01 | userland command と agent tool が同じ JSON client を共有できる |
| [x] | WFG-06 | `src/usr/command/webfetch.c` を追加し、抽出済み本文を表示できるようにする | WFG-05 | `webfetch https://...` で title / excerpt / main_text が表示される |
| [x] | WFG-07 | agent tool `fetch_url` を追加し、tool registry に登録する | WFG-05 | agent が `run_command(\"curl ...\")` ではなく narrow tool を使える |
| [x] | WFG-08 | `/etc/CLAUDE.md` と `system_prompt.txt` を更新し、`websearch` と `fetch_url` の使い分けを明記する | WFG-06, WFG-07 | 最新 Web 情報の経路がプロンプト上で固定される |
| [x] | WFG-09 | bounded output / artifact path を `fetch_url` tool result に統合する | WFG-07 | 大きい本文でも head/tail + artifact で返せる |

## M2: 安全制御

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | WFG-10 | domain allowlist と `GET` / `HEAD` 制限を実装する | WFG-02 | allowlist 外 URL や不許可 method が拒否される |
| [x] | WFG-11 | max bytes / max chars / timeout / content-type 制限を実装する | WFG-04 | 過大レスポンスや不適切な MIME を安全に切れる |
| [x] | WFG-12 | `render_js` opt-in 経路を追加し、既定では無効にする | WFG-10 | JS rendering が明示要求時だけ動く |
| [x] | WFG-13 | script / style / hidden text を含む抽出結果の sanitization を実装する | WFG-03, WFG-11 | prompt injection 面積を減らした本文が返る |

## M3: 検証と運用

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | WFG-14 | host 側 unit test と fixture page を追加する | WFG-04, WFG-13 | extract と policy の境界条件を host test で固定できる |
| [x] | WFG-15 | QEMU smoke を追加し、fetch 成功 / allowlist 拒否 / truncation を確認する | WFG-09, WFG-11 | guest 経路の主要ケースが 1 コマンドで検証できる |
| [x] | WFG-16 | mock weather endpoint を用いた weather smoke を追加する | WFG-15 | live Web 依存なしに「天気を source URL 付きで答える」経路を確認できる |
| [x] | WFG-17 | host 側 gateway の起動手順、設定、手動デモ手順を README へ追加する | WFG-15 | 開発者が search/fetch の両サービスを再現できる |

## 先送りする項目

- guest 側の汎用 HTML / DOM パーサ
- 認証付きサイトや cookie session の対応
- full browser automation を前提にした複雑な JS app
- live weather site を使った CI
