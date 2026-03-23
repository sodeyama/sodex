# Plan 03: Type System と Standard Surface

## 目的

`sx` の semantic contract を、
`sxi` runtime と将来の `sxc` compiler が共有できる形で定義する。
文法だけ先に決めても、type、scope、builtin surface が曖昧だと
runtime 実装が膨らみやすい。

## 背景

Sodex の userland は最小 libc / syscall を前提としており、
最初から rich object model や generic container を載せると重い。
一方で agent 用の言語には、file、process、JSON、text を触る最低限の surface が要る。

そこで v0 の semantic contract は、
「primitive を素直に静的化し、container と host bridge は保守的に入れる」
方向で切る。

## v0 で固定したいもの

### 1. primitive type

- `i32`
- `bool`
- `str`
- `buf`
- `unit`

`float`、`i64`、user-defined struct は後段に回す。

### 2. container

- `list`
- `map`

v0 では element typing を厳密に入れすぎず、
runtime container として扱う案を第一候補にする。
generic container は後段候補とする。

### 3. scope / mutability

- block scope
- function scope
- import unit ごとの namespace
- `let` は immutable を基本にし、mutable binding は明示構文を持つ

exact な mutable syntax はここで fix する。

### 4. builtin namespace

v0 の入口は namespace ベースで切る。

- `io`
- `fs`
- `proc`
- `json`
- `text`

global function を増やしすぎず、host bridge の責務を追いやすくする。

## error model の方針

最初から generic `Result<T>` と pattern matching を入れると、
language と runtime の両方が重くなる。
そのため v0 の第一候補は次の形にする。

- runtime / builtin failure は fail-fast で停止
- recoverable branch は `fs.exists()` や `proc.status_ok()` のような predicate helper で書く
- structured recoverable error は v1 候補に回す

これは制約ではあるが、初期の `sxi` 実装量を大きく減らせる。

## module surface

module 契約では次を固定する必要がある。

- `import` の記法
- export の最小単位
- module namespace の見え方
- cycle を禁止するか
- top-level value 初期化をどこまで許すか

v0 では cycle を禁止し、
import order と side effect を単純に保つ案を優先する。

## 非ゴール

- 例外機構、defer、RAII を初回から入れること
- struct / enum / trait / interface をまとめて入れること
- polymorphism を v0 の必須条件にすること

## 実装ステップ

1. primitive / container / unit の semantic を固定する
2. binding と mutability の規則を決める
3. builtin namespace と symbol 表現を決める
4. import / export / cycle policy を fix する
5. host test 用 semantic fixture を作る

## 変更対象

- 新規候補
  - `src/usr/include/sx_types.h`
  - `src/usr/include/sx_semantics.h`
  - `src/usr/lib/libsx/semantics.c`
  - `tests/test_sx_semantics.c`
- 関連
  - `specs/sx-language/README.md`
  - `specs/sxi-runtime/plans/03-builtins-modules-and-host-bridge.md`

## 検証

- host semantic test
  - type mismatch
  - immutable binding への再代入
  - namespace lookup
  - import cycle 拒否
- language fixture
  - file copy
  - grep-lite
  - JSON parse

## 完了条件

- primitive type と container の扱いが文書化される
- binding / scope / mutability が一貫して説明できる
- builtin namespace と import 契約が `sxi` 実装に落とせる
- compiler を追加しても semantic contract を変えずに済む
