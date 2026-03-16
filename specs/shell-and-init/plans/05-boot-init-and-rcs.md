# Plan 05: Boot Init と `rcS`

## 概要

`/usr/bin/init` が boot 後に `/etc/init.d/rcS` を実行し、
service script で daemon を起動する経路を作る。

## 起動モデル

```text
kernel
  -> /usr/bin/init
       -> /usr/bin/sh /etc/init.d/rcS
            -> /etc/init.d/sshd start
            -> ...
       -> child reap loop
```

## MVP 方針

- runlevel は持ち込まない
- `rcS` 1 本から始める
- `rcS` の中で service 起動順を明示する
- `init` は boot script 完了後も生き続け、child を reap する

## `init` の責務

- 既定環境をセットする
  - `PATH`
  - 必要なら `HOME`, `TERM`
- `/etc/init.d/rcS` の存在確認
- boot script の exit status を記録する
- zombie を回収する
- mode ごとの後続起動を分ける
  - `user`: `term` を foreground 起動するか
  - `server-headless`: service のみで待機するか

## ここでまだやらないもの

- full `inittab`
- respawn policy metadata
- `SNNfoo` / `KNNfoo` の runlevel link farm
- package install 時の `install_initd`

これらは後続候補に回す。

## `rc.common`

service script 共通関数は `rcS` からも使うので、
`/etc/init.d/rc.common` を `.` で読む前提を許可する。

例:

```sh
. /etc/init.d/rc.common
service_start sshd /usr/bin/sshd
```

## 変更対象

- 既存
  - `src/usr/init.c`
  - `src/makefile`
  - `src/tools/kmkfs.cpp`
- 新規候補
  - `/etc/init.d/rcS`
  - `/etc/init.d/rc.common`
  - test overlay 用の script fixture

## 検証

- boot 後に `rcS` が 1 回だけ実行される
- `rcS` 失敗時の fail-safe が決まっている
- `init` が child reaper として残る

## 完了条件

- `init.c` の hardcode service 起動が `rcS` に移る
- `rcS` が shell script として image に入る
- `init` が boot script 実行後も zombie を回収できる
