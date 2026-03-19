# Plan 02: guest 側 `webfetch` command と `fetch_url` tool

## 概要

host 側 `/fetch` ができても、agent が毎回 `curl` と JSON 手作業で扱う設計では
安定しない。この plan では guest 側に narrow interface を追加し、
human と agent の両方が同じ fetch 経路を使えるようにする。

## 依存と出口

- 依存: 01
- この plan の出口
  - userland command `webfetch` が使える
  - agent tool `fetch_url` が registry 経由で使える
  - `websearch` → `webfetch` / `fetch_url` の役割分担が固まる

## 現状

- guest 側に `webfetch` command は無い
- agent tool には Web 本文取得専用 entry が無い
- `run_command(\"curl ...\")` に寄ると、tool description と bounded output の両面で不安定

## 方針

- guest 側 HTTP client は既存 userland HTTP/JSON 基盤を再利用する
- `webfetch` command と `fetch_url` tool は同じ shared client を使う
- command は人間向け表示、tool は JSON 返却に寄せる
- `/etc/CLAUDE.md` と `system_prompt.txt` に使い分けを明記する

## 設計判断

- command 名は `webfetch` にする
  - `websearch` との対比が分かりやすいため
- tool 名は `fetch_url` にする
  - 役割が狭く、モデルが選びやすいため
- `render_js` は schema に含めるが既定は `false`
- tool result では `title`, `excerpt`, `main_text`, `citations`, `artifact_path` を返す

## 実装ステップ

1. `/fetch` を叩く shared client を userland 側へ追加する
2. `src/usr/command/webfetch.c` を追加する
3. `src/usr/lib/libagent/tool_fetch_url.c` を追加する
4. tool registry / schema / prompt 文言を更新する
5. bounded output と artifact path を tool result へ統合する

## 変更対象

- 新規候補
  - `src/usr/command/webfetch.c`
  - `src/usr/lib/libagent/web_fetch_client.c`
  - `src/usr/lib/libagent/tool_fetch_url.c`
  - `src/usr/include/web_fetch_client.h`
- 既存候補
  - `src/usr/command/makefile`
  - `src/usr/lib/libagent/tool_init.c`
  - `src/usr/include/agent/tool_handlers.h`
  - `src/rootfs-overlay/etc/CLAUDE.md`
  - `src/rootfs-overlay/etc/agent/system_prompt.txt`

## 検証

- `webfetch https://example.com` で抽出済み本文が表示される
- `fetch_url` が `tool_use` として呼ばれ、JSON result を返せる
- 大きい本文でも head/tail + artifact で返せる
- `websearch` の候補 URL を `webfetch` へ流す手作業が成立する

## 完了条件

- human と agent の両方が同じ構造化 fetch 経路を使える
- `curl` 直叩きではなく narrow interface へ寄せられる
- 次の policy / safety plan が guest 導線込みで進められる

