# Plan 03: Builtins / Modules / Host Bridge

## 目的

`sxi` を agent 用言語として実用にするために、
`io`、`fs`、`proc`、`json`、`text` の最小 host bridge を定義する。
この plan では runtime が guest OS の API とどう接続するかを扱う。

## 背景

Sodex にはすでに file / process / JSON / text の基盤がある。
ただし、それぞれの契約は `sx` 側で直接露出せず、
runtime が薄い facade として整える必要がある。

特に `proc` は注意が必要である。

- `execve()` は spawn 的
- `fork()` はまだ stub
- `run_command` は shell 文字列実行だが timeout / capture 制約を持つ

このため `sxi` の process bridge は、
Unix 的な `fork/exec` API を前提にせず、
spawn + wait + optional capture の明示 API に寄せるべきである。

## 初期 namespace

### `io`

- console 出力
- error 出力
- simple input

### `fs`

- `exists`
- `read_text`
- `write_text`
- `append_text`
- `list_dir`

### `proc`

- argv 指定の command 実行
- exit status 取得
- optional stdout capture

### `json`

- parse
- stringify
- field access helper

### `text`

- split
- join
- contains
- trim

regex や HTTP / network は v0 完了条件にしない。

## module 方針

- stdlib module は固定 namespace から expose する
- user module import と stdlib import を区別できるようにする
- cycle は禁止する
- stdlib 実装は C 側 builtin と `.sx` module の混在を許す

## error / failure 方針

v0 は fail-fast を基本にするが、
branch 用 predicate は用意する。

- `fs.exists(path) -> bool`
- `proc.status_ok(status) -> bool`

一方で `fs.read_text()` や `json.parse()` は失敗時に runtime error とする案を優先する。

## 非ゴール

- network socket API
- dynamic library loading
- shell string interpolation を前提にした process API
- security sandbox の新設

## 設計方針

### 1. process API は文字列 shell 実行より argv 実行を優先する

shell を中継すると quoting と error 診断が崩れやすい。
基本は argv ベースに寄せ、必要なら convenience として shell 実行を後段に回す。

### 2. current directory と path 解決を明示する

guest の file API は current directory を持つ。
`sxi` の `fs` もこれを引き継ぎ、path の解釈を shell と合わせる。

### 3. JSON は既存 parser / writer を再利用する

新しい JSON 実装を持ち込まず、既存の `json.h` / `jw_*` を bridge する。

## 実装ステップ

1. namespace ごとの関数一覧を fix する
2. `fs` と `io` を先に入れる
3. `proc` を spawn 的契約で設計する
4. `json` / `text` を bridge する
5. builtin failure と runtime failure の境界を整える

## 変更対象

- 既存
  - `src/usr/include/stdio.h`
  - `src/usr/include/stdlib.h`
  - `src/usr/include/json.h`
- 新規候補
  - `src/usr/include/sx_builtins.h`
  - `src/usr/lib/libsx/builtins_io.c`
  - `src/usr/lib/libsx/builtins_fs.c`
  - `src/usr/lib/libsx/builtins_proc.c`
  - `src/usr/lib/libsx/builtins_json.c`
  - `src/usr/lib/libsx/builtins_text.c`
  - `tests/test_sxi_builtins.c`

## 検証

- host test
  - `fs.exists`, `read_text`, `write_text`
  - `proc` exit status
  - JSON parse / stringify
  - text helper
- QEMU smoke
  - file copy script
  - grep-lite script
  - JSON parse script

## 完了条件

- `io` / `fs` / `proc` / `json` / `text` の入口が固定される
- process API が spawn 契約と矛盾しない
- file / JSON / text の代表 use case が guest で動く
- builtin failure の扱いが一貫する
