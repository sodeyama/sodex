# Sodex User Scope CLAUDE.md

Sodex guest 全体で共有する user-scope 指示です。
agent は `/etc/CLAUDE.md` を前提として扱ってください。

## 基本

- 既定の対話シェルは `eshell`、スクリプト実行や `-c` は `sh`
- 既定のホームディレクトリと起動位置は `/home/user`
- PTY/TTY ベースで動作し、UTF-8 表示と日本語入力に対応
- shell は `|`, `>`, `<`, `>>` を扱える
- フルスクリーン編集は `vi`
- 実行ファイルは主に `/usr/bin` に配置

## Sodex shell の書き方

- script は `sh file.sh` または `sh -c '...'` で実行する
- 変数代入は `name=value` の形で書き、`name = value` のように空白を入れない
- 変数参照は `$name`, `$1`, `$2`, `$?`, `$!` を使う
- `if` は `fi` で閉じる。`end` は使わない
- `for`, `while`, `until` は `do ... done` で書く
- `then` と `do` の前には `;` か改行が必要
- 条件式は `test` または `[` を使う。`[` のときは最後に `]` が必要
- 文字列比較は `=` を優先して使う

### 使える主な構文

```sh
name=value
echo "$name"

if [ "$name" = "value" ]; then
  echo ok
elif [ -z "$name" ]; then
  echo empty
else
  echo other
fi

for item in a b c; do
  echo "$item"
done

while [ -f /tmp/flag ]; do
  break
done

until [ -f /tmp/ready ]; do
  sleep 1
done
```

### `test` / `[` の最小対応

- file 存在: `[ -f path ]`, `[ -e path ]`
- directory 存在: `[ -d path ]`
- 空文字判定: `[ -n "$x" ]`, `[ -z "$x" ]`
- 文字列比較: `[ "$a" = "$b" ]`, `[ "$a" != "$b" ]`

### いま非対応として考えるもの

- shell function
- command substitution: `` `...` ``, `$(...)`
- arithmetic expansion: `$((...))`
- brace expansion
- here document
- `end` で閉じる独自構文

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
- file tool は絶対 path または相対 path を使える
- 相対 path は current directory から解決され、既定起動位置は `/home/user`
- file を書く前に、可能なら `read_file` か `cat` で現状を確認する
- `standard` mode での書き込み先は原則 `/home/user`、`/tmp`、`/var/agent`
- URL 探索だけが必要なら `websearch`
- raw HTTP を見る必要があるときだけ `curl`
- 天気、ニュース、株価、為替などの最新情報は、tool を1回以上使って確認してから答える
- プロジェクト固有の指示は、起動ディレクトリ直下の `CLAUDE.md` を優先する

## unix text tools テスト

- guest 内の初期化スクリプト: `sh /etc/init.d/rcS.unix-text`
- guest 内の追加ケース: `sh /etc/init.d/rcS.unix-text-extra`
- host 側 unit test: `make -C tests test_unix_text_tools test_usr_string`
- host 側実行: `./tests/test_unix_text_tools`
- host 側実行: `./tests/test_usr_string`
- QEMU smoke 一括実行: `make -C src test-qemu-unix-text-tools`
- QEMU smoke を直接実行: `python3 src/test/run_qemu_unix_text_tools_smoke.py build/bin/fsboot.bin build/log`

### 対象ファイル

- `src/test/data/unix_text_tools_rcS.sh`: main の smoke 用スクリプト
- `src/test/data/unix_text_tools_rcS_extra.sh`: `find`, `diff`, `tee`, long option 追加ケース
- `src/test/run_qemu_unix_text_tools_smoke.py <fsboot> <logdir>`: QEMU 起動と検証
- `tests/test_unix_text_tools.c`: host 側の GNU 互換引数テスト
- `tests/test_usr_string.c`: userland libc `strcpy` 回帰テスト
