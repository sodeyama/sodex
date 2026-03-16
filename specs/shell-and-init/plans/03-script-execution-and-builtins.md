# Plan 03: Script 実行と Built-in

## 概要

generic shell script を guest 内で書いて実行できるようにする。
この phase では parser だけでなく、
変数、built-in、script file 実行、direct script 実行を入れる。

## 必要な機能

- `sh file`
- `sh -c '...'`
- positional parameter
  - `$0`, `$1`, `$2`, `$@`, `$*`
- status / pid
  - `$?`, `$!`
- variable assignment
- `export`
- `set`
- current shell built-in
  - `cd`
  - `exit`
  - `.`
  - `wait`
  - `trap`

## `.` が必要な理由

init script 共通関数を持たせるとき、
`/etc/init.d/rc.common` のような file を
current shell 環境で読み込める必要がある。

そのため `.` は後回しにしない。

## direct script 実行

最初の実行経路は `sh script` でよい。
ただし最終的には次のどちらかが必要になる。

1. `#!` interpreter line を kernel/userland exec path で解釈する
2. `ENOEXEC` 相当時に `sh script` へ fallback する

POSIX の command search/exec では `ENOEXEC` 時の shell fallback があるため、
これに寄せるのが自然である。

## PATH と環境

- `PATH` を shell variable / exported env として持つ
- init script は外部環境に依存しないよう、既定 `PATH` を shell/init 側で固定する
- `open_env()` の固定 `/usr/bin` 探索だけでは足りないので、段階的に shell 管理へ寄せる

## 変更対象

- 既存
  - `src/elfloader.c`
  - `src/usr/include/stdlib.h`
  - `src/usr/lib/libc/stdio.c`
  - `src/usr/command/eshell.c`
- 新規候補
  - `src/usr/lib/libc/shell_vars.c`
  - `src/usr/lib/libc/shell_builtins.c`
  - `src/usr/lib/libc/shell_script.c`

## 検証

- `sh /etc/init.d/test start` が実行できる
- `. /etc/init.d/rc.common` が current shell に反映される
- `false; echo $?` や `sleep & echo $!` 相当が動く
- `./script.sh` または `foo` の direct 実行方針が固定される

## 完了条件

- service script に必要な variable/builtin が揃う
- `sh file` と `sh -c` が通る
- `.` / `wait` / `trap` を current shell で実行できる
- direct script 実行の実装方針が固まる
