# Sodex User Scope CLAUDE.md

このファイルは Sodex guest 全体で共有する user-scope の指示です。
agent は起動時に `/etc/CLAUDE.md` を読み、現在のシステムで出来ることの前提として扱ってください。

## 現在のシェルで出来ること

- 既定の対話シェルは `eshell`、スクリプト実行や `-c` には `sh` が使えます
- PTY/TTY ベースで動作し、UTF-8 表示と日本語入力に対応しています
- shell 構文として `|`, `>`, `<`, `>>` が使えます
- quoting、escape、複数段 pipeline を扱えます
- `vi` でフルスクリーン編集ができます

## 主なコマンド

- ファイル操作: `ls`, `cat`, `touch`, `mkdir`, `rm`, `rmdir`, `mv`, `cd`, `pwd`
- プロセス確認: `ps`, `kill`, `sleep`
- ネットワーク: `ping`, `dig`, `curl`
- サービス関連: `service`, `start-stop-daemon`, `sshd`
- LLM/補助: `agent`, `ask`, `claude`

全コマンドは `/usr/bin/` に配置されています（PATH=/usr/bin）。

## curl の使い方

Sodex独自実装のcurlコマンドです。HTTP/HTTPS(TLS 1.2)に対応しています。

### 基本

```
curl https://httpbin.org/get
curl http://10.0.2.2:8080/healthz
curl -v https://example.com/api
```

### オプション

- `-X METHOD` : HTTPメソッド指定（GET/POST/PUT/DELETE）
- `-d DATA` : リクエストボディ（指定するとPOSTがデフォルト）
- `-H "Name: Value"` : ヘッダー追加（複数指定可）
- `-v` : レスポンスヘッダーを表示
- `-o FILE` : レスポンスをファイルに保存

### POST リクエスト例

```
curl -X POST -H "Content-Type: application/json" -d '{"key":"value"}' https://httpbin.org/post
```

### 制約事項

- **小さいAPIレスポンス（JSON等）は完全に取得可能**
- 大きいHTMLページ（Yahoo等）は先頭数KBで途切れる場合がある（TCP半閉鎖の制約）
- Transfer-Encoding: chunked のチャンクサイズ値が出力に混ざることがある
- DNS解決はQEMU SLiRPのDNS (10.0.2.3) を使用
- ホストマシンへのアクセスは `10.0.2.2` 経由

## 端末と編集

- `term` 上で shell と `vi` を使えます
- 日本語ファイル名を扱えます
- `vi` は保存、undo/redo、検索、visual mode を含む基本編集が使えます

## 制約

- POSIX 完全互換ではありません
- 外部パッケージマネージャや一般的な Unix ユーティリティ一式はありません
- 大きい出力は扱いにくいので、短いコマンドに分けて調査してください
- ファイル変更前は既存内容を確認してください
- 絶対パスを優先してください

## agent への指示

- まず現在あるコマンドで確認してから推測してください
- 不必要に長い出力を 1 回で取らず、対象を絞って段階的に確認してください
- プロジェクト固有の指示は、起動ディレクトリ直下の `CLAUDE.md` を優先して従ってください
