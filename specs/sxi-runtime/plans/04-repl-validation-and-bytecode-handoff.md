# Plan 04: REPL / Validation / Bytecode Handoff

## 目的

`sxi` を日常利用できる command にするために、
REPL UX、host/QEMU test、agent workflow、将来の bytecode handoff 境界を固定する。

## 背景

source interpreter は「動く」だけでは足りず、
修正ループと回帰検知の導線が必要である。
特に今回の目的では、agent が guest 内で file を書き換えながら
短いループで試せることが重要になる。

また v0 は tree-walk evaluator だが、
後で `sxb` bytecode VM へ進める余地も残したい。

## REPL scope

### command

- `:quit`
- `:reset`
- `:load path`

必要なら `:help` を追加する。

### input

- multi-line block
- incomplete input の継続
- last status の可視化

### state

- session arena に定義を保持
- `:reset` で破棄
- loaded module cache を session にぶら下げるかはここで決める

## validation 方針

### host

- lexer / parser / semantics fixture
- evaluator core
- builtin bridge
- CLI mode dispatch

### QEMU

- `sxi --check`
- `sxi hello.sx`
- file copy
- grep-lite
- JSON parse

### agent workflow

最低限次を smoke に入れる。

1. `write_file` で `.sx` を保存
2. `run_command` で `sxi --check`
3. `run_command` で `sxi script.sx`
4. failure 修正後の再実行

## bytecode handoff

v0 では bytecode をまだ作らないが、
次の境界は最初から意識する。

- AST の後に lowering 可能な evaluator interface
- constant table 相当を後から切り出せる value representation
- builtin call を opcode 化しやすい dispatch point

`sxb` を後段に作るとき、`sxi` の CLI や stdlib surface を壊さない形を目指す。

## 非ゴール

- debugger / profiler 完備
- REPL history 永続化
- bytecode cache 実装そのもの

## 実装ステップ

1. REPL command と multi-line policy を fix する
2. host / QEMU / agent workflow の test matrix を作る
3. sample script と docs を整える
4. evaluator backend の handoff 境界を文書化する

## 変更対象

- 新規候補
  - `src/usr/command/sxi.c`
  - `tests/test_sxi_repl.c`
  - `tests/test_sxi_cli.c`
  - `src/test/run_qemu_sxi_smoke.py`
  - `src/test/run_qemu_sxi_agent_smoke.py`
- 文書
  - `README.md`
  - `specs/sx-language/README.md`
  - `specs/sxi-runtime/README.md`

## 検証

- host test
  - REPL incomplete input
  - `:reset` 後の state 初期化
  - CLI exit code
- QEMU smoke
  - `sxi --check`
  - representative sample
- agent smoke
  - `write_file` -> `sxi --check` -> `sxi run` -> fix loop

## 完了条件

- REPL UX と session 寿命管理が文書化される
- host / QEMU / agent workflow の回帰経路がそろう
- sample script が docs と smoke で共有される
- `sxb` / `sxc` へ進むための backend 境界が説明できる
