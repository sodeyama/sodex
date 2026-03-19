# Sodex User Scope CLAUDE.md

Sodex guest 全体で共有する user-scope 指示です。
agent は `/etc/CLAUDE.md` を前提として扱ってください。

## 基本

- 既定の対話シェルは `eshell`、スクリプト実行や `-c` は `sh`
- PTY/TTY ベースで動作し、UTF-8 表示と日本語入力に対応
- shell は `|`, `>`, `<`, `>>` を扱える
- フルスクリーン編集は `vi`
- 実行ファイルは主に `/usr/bin` に配置

## 主なコマンド

- ファイル操作: `ls`, `cat`, `touch`, `mkdir`, `rm`, `rmdir`, `mv`, `cd`, `pwd`
- プロセス確認: `ps`, `kill`, `sleep`
- ネットワーク: `ping`, `dig`, `curl`, `websearch`, `webfetch`
- LLM/補助: `agent`, `ask`, `claude`
- サービス関連: `service`, `start-stop-daemon`, `sshd`

## Web の使い分け

- URL が未確定な調査: `websearch`
- URL が確定しており、抽出済み本文や title が欲しい: `webfetch`
- 生の HTTP ヘッダー、本文、API 動作確認: `curl`

### 例

```sh
websearch tokyo weather
webfetch https://www.jma.go.jp/
webfetch -m 500 https://tenki.jp/forecast/3/16/4410/13101/
curl -v http://10.0.2.2:8080/healthz
```

## webfetch の注意

- host 側の structured web gateway を使う
- `user` / `server` / `server-headless` の通常起動では host 側 gateway が自動起動される
- 既定の接続先は `10.0.2.2:8081/fetch`
- `net` mode では `10.0.2.2` が使えないため、必要なら `-h <host-ip>:8081` で上書きする
- allowlist 外 URL や不許可 method は拒否される
- 大きい本文は `main_text` が切り詰められる
- `-I` で `HEAD`、`-r` で JS rendering 要求、`-m` で抽出文字数上限を指定できる

## curl の注意

- 小さい JSON / API 応答の確認には向く
- 大きい HTML は途中で切れることがある
- chunked 応答ではチャンクサイズが混ざることがある
- host へのアクセスは `10.0.2.2`

## agent への指示

- まず既存コマンドか tool で確認してから推測する
- 長い出力は分割して取得する
- URL が確定していれば `fetch_url` / `webfetch` を優先する
- URL 探索だけが必要なら `websearch`
- raw HTTP を見る必要があるときだけ `curl`
- 天気、ニュース、株価、為替などの最新情報は、tool を1回以上使って確認してから答える
- プロジェクト固有の指示は、起動ディレクトリ直下の `CLAUDE.md` を優先する
