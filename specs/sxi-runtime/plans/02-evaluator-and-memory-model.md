# Plan 02: Evaluator と Memory Model

## 目的

tree-walk interpreter としての `sxi` core を定義し、
value 表現、environment、call frame、arena、runtime error を固定する。
ここが曖昧だと builtins と REPL の寿命管理が破綻しやすい。

## 背景

Sodex の userland では `malloc` / `free` はあるが、
初手から tracing GC を入れると実装量が大きい。
また REPL は長寿命 state を持つため、
1 script 実行ごとの一括破棄と session 継続の両立を考える必要がある。

## v0 方針

### 1. evaluator

- AST を直接歩く tree-walk evaluator
- bytecode はまだ持たない
- statement / expression / declaration を段階的に評価する

### 2. value model

tagged union を基本にする。

- immediate
  - `i32`
  - `bool`
  - `unit`
- heap-backed
  - `str`
  - `buf`
  - `list`
  - `map`
  - function / module handle

### 3. memory model

v0 は GC なしで始める。

- script arena
  - 1 回の `sxi file.sx` / `-e` 実行単位で作成
  - 終了時にまとめて破棄
- session arena
  - REPL の定義や loaded module を保持
  - `:reset` で明示破棄

### 4. call frame

- function call ごとに frame を積む
- local binding table を持つ
- recursion / frame depth に hard limit を設ける

## runtime error 方針

- source span 付き message
- call stack 表示
- builtin failure と evaluator failure の区別
- fail-fast で停止

structured exception は v0 では持たない。

## 非ゴール

- mark-sweep GC
- closure capture
- TCO や optimizer
- debugger と step 実行

## 設計方針

### 1. value と AST を密結合にしすぎない

bytecode 化を見据え、value と evaluator state は
AST node への一時ポインタに依存しすぎないようにする。

### 2. resource handle は runtime object として管理する

file や process capture buffer を使う builtins を考えると、
単なる string だけでは足りない。
必要な handle は runtime object として table 管理する。

### 3. REPL は `:reset` 前提で寿命を制御する

GC なしで長寿命化すると leak しやすい。
まずは session arena と reset command の組み合わせで運用する。

## 実装ステップ

1. tagged value と environment を定義する
2. call frame と block scope を実装する
3. script/session arena を分ける
4. runtime error と stack trace 契約を定める
5. evaluator の host test を追加する

## 変更対象

- 新規候補
  - `src/usr/include/sx_value.h`
  - `src/usr/include/sx_runtime.h`
  - `src/usr/lib/libsx/value.c`
  - `src/usr/lib/libsx/runtime.c`
  - `src/usr/lib/libsx/eval.c`
  - `tests/test_sxi_eval.c`
  - `tests/test_sxi_runtime.c`

## 検証

- host evaluator test
  - literal / operator / call
  - nested scope
  - recursion limit
  - list / map の基本操作
- runtime error test
  - unknown symbol
  - type mismatch
  - builtin failure

## 完了条件

- value model と arena model が 1 つの文書で説明できる
- tree-walk evaluator が host test で回帰検知できる
- runtime error に span と stack trace が付く
- REPL 寿命管理の基礎として session arena / `:reset` が説明できる
