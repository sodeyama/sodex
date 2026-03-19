# Plan 04: 検証基盤と weather smoke

## 概要

Web fetch は live site に依存すると壊れ方が追いにくい。
この plan では host unit test、QEMU smoke、mock weather endpoint、
手動 live demo を分けて、継続検証可能な形へ落とし込む。

## 依存と出口

- 依存: 02, 03
- この plan の出口
  - host/QEMU の両方で代表シナリオを検証できる
  - weather を含む最新情報ユースケースの回帰が追える
  - 開発者が search/fetch gateway を再現できる

## 現状

- `websearch` 専用 smoke はあるが、本文取得の smoke は無い
- live weather site に依存すると、HTML 変更や地域差で CI が不安定になる
- host 側 gateway の起動手順も未定義

## 方針

- pure logic は host test、guest 経路は QEMU smoke で見る
- CI / 定常 smoke は mock page / mock weather を使う
- live site を使うのは手動デモに留める
- 成功系だけでなく、deny / truncation / timeout も固定する

## 設計判断

- weather smoke は mock weather JSON / HTML fixture を使う
  - 「today」の絶対日付や source URL を固定しやすいため
- QEMU smoke は 1 つの大きな live script にしない
  - fetch 成功、allowlist 拒否、artifact の 3 系統に分ける
- 手動デモ手順では live fetch の日付つき出力を確認する

## 実装ステップ

1. host 側 fixture と unit test を追加する
2. fetch 成功 / deny / truncation の QEMU smoke を追加する
3. mock weather endpoint を追加する
4. weather ユースケースの smoke を追加する
5. 起動手順と手動デモ手順を README へ追記する

## 変更対象

- 新規候補
  - `tests/test_web_fetch_extract.py`
  - `tests/test_web_fetch_policy.py`
  - `src/test/run_qemu_webfetch_smoke.py`
  - `src/test/run_qemu_agent_weather_smoke.py`
  - `src/test/mock_web_fetch_server.py`
- 既存候補
  - `src/makefile`
  - `README.md`
  - `searxng/README.md`

## 検証

- static HTML fixture の抽出テストが通る
- allowlist 拒否と truncation が QEMU で再現できる
- mock weather endpoint で source URL 付き回答まで辿れる
- 手動 live demo 手順で最新日付の weather 取得を確認できる

## 完了条件

- Web fetch 基盤が一時的な bring-up ではなく継続監視できる
- weather を入口にした「検索して本文を取り、source 付きで答える」経路が固定される
- 後続の専用 weather tool や MCP 化がこの検証基盤を前提に進められる

