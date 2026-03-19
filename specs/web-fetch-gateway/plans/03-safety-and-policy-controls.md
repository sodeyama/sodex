# Plan 03: 安全制御と policy

## 概要

Web fetch は prompt injection と過大レスポンスの入口になりやすい。
この plan では unrestricted fetch を避け、allowlist、method 制限、
timeout / size 制限、`render_js` opt-in、抽出 sanitization を固める。

## 依存と出口

- 依存: 01, 02
- この plan の出口
  - Web gateway が deny-by-default で動く
  - live Web 取得時の危険面積を抑えられる
  - `fetch_url` tool が bounded output 前提で安全に返る

## 現状

- `curl` はサイト単位の allowlist を持たない
- `websearch` は本文取得をしないので、policy は限定的で済んでいる
- agent prompt にも `websearch` / `curl` の役割まではあるが、fetch policy は無い

## 方針

- 既定は deny-all
- 許可 domain の allowlist を設定で開ける
- 許可 method は `GET` / `HEAD` のみ
- `render_js` は明示要求時だけ
- HTML からは script / style / hidden text を落とした本文だけを返す

## 設計判断

- policy は host 側で判定する
  - guest 側で複雑な URL / HTML 判定をしないため
- `render_js` は別経路に分ける
  - static fetch と混ぜると debug が難しくなるため
- content-type は whitelist 方式にする
  - `text/html`, `text/plain`, `application/json` を初手候補とする
- artifact へ raw HTML を残す場合でも、LLM へは既定で渡さない

## 実装ステップ

1. allowlist と `GET` / `HEAD` 制限を定義する
2. max bytes / max chars / timeout / content-type 制限を実装する
3. `render_js` opt-in の設定面を追加する
4. HTML 抽出結果の sanitization を実装する
5. エラー JSON と audit しやすい failure code を整理する

## 変更対象

- 新規候補
  - `src/tools/web_fetch_policy.py`
  - `src/tools/web_fetch_config.example.json`
- 既存候補
  - `src/usr/lib/libagent/tool_fetch_url.c`
  - `src/rootfs-overlay/etc/CLAUDE.md`
  - `src/rootfs-overlay/etc/agent/system_prompt.txt`

## 検証

- allowlist 外 URL が拒否される
- `POST` や不許可 scheme が拒否される
- 過大レスポンスが timeout / size 制限で安全に切られる
- HTML 中の `<script>` や style 由来ノイズが本文に入らない
- `render_js` を要求しない限り静的経路だけが使われる

## 完了条件

- generic fetch をオンにしても危険側に倒れにくい
- policy 逸脱時の失敗理由を guest / agent が扱える
- validation plan がこの制御込みで組める

