# Plan 01: Command Surface と Frontend Integration

## 目的

`/usr/bin/sxi` の入口を先に固定し、
`sx` frontend を `libsx` として切り出して実行系に組み込む。
ここが曖昧だと、file 実行、REPL、将来の compiler が別々の parser を持ちやすい。

## 背景

Sodex では command は `src/usr/command/` に載り、
static userland binary として build される。
`sxi` も同じ経路に載せるのが自然だが、
parser / AST / semantics は command に埋め込まず共有ライブラリ化したい。

また agent workflow を考えると、
単なる `sxi file.sx` だけでなく次が要る。

- `sxi -e '...'`
- `sxi --check file.sx`
- `sxi` REPL

`--check` は compile をまだ持たない段階でも、
source と semantic の妥当性だけを先に確認できるため有用である。

## 初期 scope

### CLI

- `sxi file.sx`
- `sxi -e 'code'`
- `sxi --check file.sx`
- `sxi`

usage error、source error、runtime error の exit code はここで固定する。

### build layout

- `src/usr/command/sxi.c`
- `src/usr/include/sx_*.h`
- `src/usr/lib/libsx/*.c`

を基本構成にし、`src/usr/command/makefile` から link する。

### source loader

- current directory 基準で script を開く
- relative import を current file 基準で解決する
- stdlib module は `/usr/lib/sx` など固定 subtree に置く候補とする

## 非ゴール

- compiler driver と interpreter driver を同時に作ること
- package manager や global module cache を最初から作ること
- shebang 実行や shell fallback を v0 完了条件にすること

## 設計方針

### 1. `libsx` を command から分離する

lexer / parser / AST / semantics は `sxi.c` に入れず、
shared frontend として切り出す。
これにより `sxc` 追加時の複製を防ぐ。

### 2. `--check` を v0 から持つ

agent との相性上、実行前に syntax / semantic を高速確認できるとよい。
compile が無くても `--check` は価値が高い。

### 3. module search path は単純に始める

v0 では次だけでよい。

- current file directory
- current working directory
- fixed stdlib directory

環境変数ベースの module path は後段に回す。

## 実装ステップ

1. `sxi` CLI mode と usage を fix する
2. `libsx` の include / object / archive 位置を決める
3. source loader と module search path を定義する
4. REPL と `--check` が同じ frontend を通るようにする

## 変更対象

- 既存
  - `src/usr/command/makefile`
  - `src/usr/lib/libc/makefile`
- 新規候補
  - `src/usr/command/sxi.c`
  - `src/usr/lib/libsx/makefile`
  - `src/usr/lib/libsx/lexer.c`
  - `src/usr/lib/libsx/parser.c`
  - `src/usr/lib/libsx/semantics.c`
  - `tests/test_sx_lexer.c`
  - `tests/test_sx_parser.c`

## 検証

- host test
  - CLI mode dispatch
  - `--check` success / failure
  - source loader の relative import 解決
- guest smoke
  - `sxi hello.sx`
  - `sxi -e '...'`
  - `sxi --check broken.sx`

## 完了条件

- `/usr/bin/sxi` の 4 入口が文書と実装で一致する
- parser / semantics が `libsx` に分離される
- source loader と module path の基準が一意になる
- file 実行と REPL が同じ frontend を通る
