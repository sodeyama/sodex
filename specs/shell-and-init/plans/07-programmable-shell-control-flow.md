# Plan 07: プログラマブル Shell 制御構文

2026-03-22 実装済み。host unit test と QEMU smoke で回帰を固定した。

## 概要

現状の `sh` / `eshell` は、
list、`&&` / `||`、background、変数、script 実行までは揃っているが、
compound command を持たないため、
通常の shell script らしい条件分岐や繰り返しはまだ書けない。

この phase では、shell を「service script 実行器」から
より一般的な script 実行系へ広げる。
最低限、次を guest 内で成立させる。

- `if ... then ... elif ... else ... fi`
- `for name in ...; do ...; done`
- `while ...; do ...; done`
- `until ...; do ...; done`
- `break`, `continue`
- `test` / `[` の最小 subset
- `eshell` の継続 prompt と multi-line 入力

## 背景

現在の shell core は flat な pipeline/list model を前提にしている。

- parser は `shell_program -> pipeline[]` の平坦構造で、
  `if` や loop の入れ子を持てない
- executor は pipeline 単位の exit status 連結しか持たず、
  `break` / `continue` のような制御伝播先がない
- `eshell` は 1 回の `read()` で 1 行ずつ実行するため、
  `if ... then` の継続入力や `do` / `done` の待機ができない
- `/usr/bin/test` は stub のままで、
  shell script の条件判定に実用な predicate が足りない

このため、変数や `sh file` があっても、
実際の script は直列 command 群にとどまりやすい。

## 初期 scope

### 1. 制御構文

- `if list; then list; fi`
- `if list; then list; elif list; then list; else list; fi`
- `for name in word...; do list; done`
- `while list; do list; done`
- `until list; do list; done`
- `break`
- `continue`

### 2. 条件判定

- `test` builtin / command
- `[` builtin
  - closing `]` を必須にする
- 最小 predicate
  - `-n`, `-z`
  - `=`, `!=`
  - `-f`, `-d`, `-e`
- 条件の真偽は command exit status で統一する

### 3. 対話入力

- `eshell` が incomplete な compound command を検出できる
- incomplete 時は parse error にせず continuation prompt へ移る
- `done`, `fi` などで構文が閉じた時点でまとめて実行する

## 非ゴール

- shell function 定義と `return`
- `case`, `select`
- arithmetic expansion / arithmetic `for`
- command substitution `` `...` `` / `$(...)`
- here-document / here-string
- glob、brace expansion、array、`local`
- POSIX shell 全量互換

## 設計方針

### 1. flat pipeline model を compound AST へ拡張する

`if` や loop を pipeline の特殊値として押し込まず、
simple command / pipeline / compound command を持つ AST に上げる。

少なくとも次の node 種別が必要になる。

- simple command
- pipeline
- sequential / AND-OR list
- `if`
- `for`
- `while`
- `until`

body は再帰的に list を持てるようにし、
入れ子の `if` や loop を扱えるようにする。

### 2. parser は syntax error と incomplete input を分ける

`eshell` で実用にするには、
`if true; then` の段階を parse error 扱いしてはいけない。

そのため parser は少なくとも次を返せる必要がある。

- success
- syntax error
- incomplete input

front-end 側は incomplete のときだけ continuation prompt を出し、
追加行を貯めて再 parse する。

### 3. 予約語は位置依存で扱う

`if`, `then`, `do`, `done` などは
常に keyword ではなく command position でだけ keyword として扱う。

これにより次のような既存挙動を壊しにくくする。

- `echo if`
- `PATH=/tmp/if`
- quoted / escaped word

### 4. 実行結果は status だけでなく制御要求を持つ

`break` / `continue` は単なる exit status では表せないため、
executor 内部では次のような制御結果を持つ。

- status
- break 要求
- continue 要求
- exit 要求

loop 本体はこの制御結果を受け取り、
どこで消費するかを明確にする。
初期実装は `break` / `continue` の 1 段だけを対象にしてよい。

### 5. loop 変数は既存の shell variable を再利用する

`for name in ...` の loop 変数は、
新しい別スコープを急いで作らず、
既存の `shell_var_set()` を通して current shell 変数として更新する。

これにより次の性質を揃えやすい。

- loop 本体から `$name` を見える
- loop 後も最後の値が残る
- built-in / sourced script と同じ variable 経路を使う

### 6. `test` / `[` は shared helper で実装する

条件判定は shell script の土台なので、
`test` と `[` を別実装にしない。

- 引数解釈は shared helper へ寄せる
- shell builtin と `/usr/bin/test` で同じ helper を使う
- `[` は最終 token の `]` を検証して helper に委譲する

## 実装ステップ

1. shell AST を compound command 対応へ拡張する
2. parser に keyword / body / incomplete input 判定を入れる
3. executor に `if` / loop / `break` / `continue` を追加する
4. `test` / `[` の最小実装を入れる
5. `eshell` に continuation prompt と multi-line buffer を入れる
6. host test / QEMU smoke / docs を更新する

## 変更対象

- 既存
  - `src/usr/command/eshell.c`
  - `src/usr/command/sh.c`
  - `src/usr/command/test.c`
  - `src/usr/include/shell.h`
  - `src/usr/lib/libc/shell_parser.c`
  - `src/usr/lib/libc/shell_executor.c`
  - `src/usr/lib/libc/shell_script.c`
  - `src/usr/lib/libc/shell_vars.c`
  - `tests/test_shell_core.c`
- 新規候補
  - `src/usr/lib/libc/shell_ast.c`
  - `src/usr/lib/libc/shell_test.c`
  - `tests/test_shell_programmable.c`
  - `src/test/run_qemu_shell_programmable_smoke.py`

## 検証

- host parser test
  - `if`, nested `if`, `for`, `while`, `until` の AST が崩れない
  - incomplete input と syntax error を区別できる
- host executor test
  - 条件分岐、0 回 / 複数回 loop、`break`, `continue` を回帰検知できる
- host `test` helper test
  - `-n`, `-z`, `=`, `!=`, `-f`, `-d`, `-e` を確認する
- QEMU smoke
  - `sh` script で file 存在判定と loop が動く
  - `eshell` で multi-line `if` と `for` を入力できる

## 完了条件

- `if` / `elif` / `else` / `fi` が `sh` script と対話 shell の両方で動く
- `for ... in`, `while`, `until` が guest 内 script で実用になる
- `break` / `continue` が loop 制御として機能する
- `test` / `[` の最小 subset で file 判定と文字列判定を書ける
- `eshell` が continuation prompt 付きで multi-line compound command を扱える
- host test と QEMU smoke が green になる
