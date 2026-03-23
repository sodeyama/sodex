# Plan 05: Expression と Control Flow の拡張

## 目的

`sx` を「小さい glue script」から「少し長い手続き型 script」へ広げるために、
演算子、assignment、`break` / `continue`、`for` を追加する。

## 背景

現状の `sx` は、`fn`、`if`、`while`、call、literal は書けるが、
次の不足がある。

- `i = i + 1` のような更新ができない
- 比較や論理式を builtin call に寄せるしかない
- `while` からの早期脱出やスキップができない
- counter loop を自然に書きづらい

これでは agent が file 変換や小さい判定ロジックを書くには十分でも、
少し複雑な制御を書くと急に冗長になる。

## 今回の scope

### 1. expression

- grouping: `(expr)`
- unary: `!expr`, `-expr`
- arithmetic: `+`, `-`, `*`, `/`, `%`
- comparison: `<`, `<=`, `>`, `>=`
- equality: `==`, `!=`
- logical: `&&`, `||`

型ルールは初期実装では次に固定する。

- arithmetic は `i32` 同士
- comparison は `i32` 同士
- equality は `bool` / `i32` / `str` の同種比較のみ
- logical は `bool`
- short-circuit は `&&` / `||` に入れる

### 2. statement

- assignment statement: `name = expr;`
- `break;`
- `continue;`
- `for (init; condition; step) { ... }`

`for` は C 風だが、初期実装では script 向けに次へ絞る。

- `init` は `let` / assignment / call / empty
- `condition` は expression / empty
- `step` は assignment / call / empty
- empty condition は `true` と同義

### 3. runtime

- assignment は最も近い scope の binding を更新する
- `break` / `continue` は loop 内だけ許す
- `for` header の `let` は loop 専用 scope に閉じる
- loop は `while` と同じ反復上限で止める

## 非ゴール

- assignment expression
- ternary operator
- compound assignment (`+=`, `-=`)
- increment / decrement (`++`, `--`)
- `switch`
- `for-in`, list literal, map literal

## parser / AST 方針

- expression parser は precedence 段階の recursive descent とする
- AST には unary / binary node を追加する
- unary / binary の子は expression index で参照する
- statement には assign / break / continue / for を追加する

## runtime 方針

- `sx_runtime_check_program()` と execute path で同じ evaluator を使う
- `&&` / `||` は short-circuit する
- `for` は内部的に init -> condition -> body -> step の順で回す
- `continue` は `for` では step を通してから次の condition へ進む

## サンプルと fixture

最低限、次を guest 同梱 sample と host/QEMU fixture に入れる。

- arithmetic / comparison / logical
- `while` + assignment
- `break` / `continue`
- `for` counter loop
- nested condition と grouping

## 変更対象

- 文書
  - `specs/sx-language/README.md`
  - `specs/sx-language/TASKS.md`
  - `specs/sxi-runtime/TASKS.md`
- 実装
  - `src/usr/include/sx_lexer.h`
  - `src/usr/include/sx_parser.h`
  - `src/usr/lib/libsx/lexer.c`
  - `src/usr/lib/libsx/parser.c`
  - `src/usr/lib/libsx/runtime.c`
- 検証
  - `tests/test_sx_lexer.c`
  - `tests/test_sx_parser.c`
  - `tests/test_sxi_runtime.c`
  - `src/test/run_qemu_sxi_smoke.py`
  - `src/rootfs-overlay/home/user/sx-examples/`

## 完了条件

- operator precedence と grouping が host test で固定される
- assignment / `break` / `continue` / `for` が host runtime test と QEMU smoke を通る
- guest 同梱 sample が README から辿れて、そのまま `sxi` で動かせる
