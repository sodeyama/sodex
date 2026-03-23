# SXI Runtime Spec

`sxi` は、`sx` source を Sodex 内で直接実行する interpreter / runtime。
この spec は `sx` 実行系を扱い、言語仕様そのものは
[`specs/sx-language/`](../sx-language/README.md) に分離する。

初期判断の前提は、次の調査に置く。

- [guest native programming environment research](../../doc/research/sodex_guest_native_programming_environment_research_2026-03-22.md)
- [guest runtime interpreter research](../../doc/research/sodex_guest_runtime_interpreter_research_2026-03-22.md)

## 背景

`sxi` を独立 spec として切る理由は、
「language spec と runtime 実装の責務を分け、source interpreter を最短で成立させる」
ためである。
現状の Sodex には、runtime を置くための基盤はかなりある。

- `/usr/bin/sh` と script 実行
- `vi`、UTF-8、IME
- ext3 と `/home/user`
- `agent` の file read/write と `run_command`
- userland libc の `open/read/write/execve/waitpid/malloc`

一方で、そのまま外部 runtime を移植するには次の制約がある。

- `agent` の `write_file` は 3072 bytes 上限で、巨大 source 生成は不利
- `agent` の `run_command` は `sh -c` 経由で約 10 秒 timeout
- `execve()` は自己置換ではなく spawn 的で、process API はその前提を意識する必要がある
- userland libc は最小で、host 由来の大きい runtime 前提を置きにくい
- shell は glue には良いが、複雑な値モデルや host bridge を持ちにくい

関連箇所:

- [`README.md`](../../README.md)
- [`src/usr/lib/libagent/tool_run_command.c`](../../src/usr/lib/libagent/tool_run_command.c)
- [`src/usr/lib/libagent/tool_write_file.c`](../../src/usr/lib/libagent/tool_write_file.c)
- [`src/usr/include/stdlib.h`](../../src/usr/include/stdlib.h)
- [`src/usr/include/stdio.h`](../../src/usr/include/stdio.h)
- [`src/usr/include/fs.h`](../../src/usr/include/fs.h)
- [`src/execve.c`](../../src/execve.c)
- [`src/usr/lib/crt0.S`](../../src/usr/lib/crt0.S)
- [`specs/memory-scaling/README.md`](../memory-scaling/README.md)

## ゴール

- `/usr/bin/sxi` を guest 内 command として追加する
- `sxi file.sx`, `sxi -e '...'`, `sxi --check file.sx`, `sxi` REPL を持つ
- `sx` frontend を共通 `libsx` として切り出し、将来の `sxc` と共有する
- v0 は tree-walk source interpreter で成立させる
- file / process / fd / path / time / JSON / text / network の standard surface を持つ
- host unit test と QEMU smoke で実行系の回帰を継続確認できる

## 非ゴール

- Lua / Wren / Wasm runtime を初手でそのまま移植しない
- JIT、AOT、bytecode cache を v0 完了条件にしない
- threads、fibers、GC、debugger を最初から完装しない
- shell を runtime の内部実装に流用しすぎない
- package manager、network stdlib、外部 FFI 全量を同時に入れない

## 目標アーキテクチャ

```text
/usr/bin/sxi
   │
   ├─ CLI / REPL frontend
   │
   ├─ libsx
   │   ├─ lexer
   │   ├─ parser
   │   ├─ AST
   │   └─ semantics / diagnostics
   │
   ├─ evaluator
   │   ├─ value model
   │   ├─ call frame
   │   ├─ environment
   │   └─ runtime error
   │
   └─ builtin host bridge
       ├─ io
       ├─ fs
       ├─ proc
       ├─ json
       └─ text
```

## v0 の runtime 方針

- first implementation は tree-walk interpreter
- frontend は `libsx` として `sxi` 本体から分離する
- value は tagged union ベースで持つ
- 初手は GC を入れず、script/session arena と明示 reset で進める
- process API は現状の spawn 的 `execve()` / `waitpid()` に合わせつつ、必要な `fork()` / `pipe()` は guest から使える最小実装まで補完する
- REPL は `:reset` を持ち、長寿命 object 問題を制御する
- runtime error は fail-fast で止め、stack trace と source span を返す

## Plans

| # | ファイル | 概要 | 依存 | この plan の出口 |
|---|---|---|---|---|
| 01 | [plans/01-command-surface-and-frontend-integration.md](plans/01-command-surface-and-frontend-integration.md) | `/usr/bin/sxi` の CLI、source loader、`libsx` の組み込み方を固める | [`specs/sx-language/plans/01-source-contract-and-language-goals.md`](../sx-language/plans/01-source-contract-and-language-goals.md), [`specs/sx-language/plans/02-grammar-and-ast.md`](../sx-language/plans/02-grammar-and-ast.md) | file / `-e` / `--check` / REPL の入口が固定される |
| 02 | [plans/02-evaluator-and-memory-model.md](plans/02-evaluator-and-memory-model.md) | value、environment、call frame、arena、runtime error を定義する | 01, [`specs/sx-language/plans/03-type-system-and-standard-surface.md`](../sx-language/plans/03-type-system-and-standard-surface.md) | source interpreter core が host test 可能な形で固まる |
| 03 | [plans/03-builtins-modules-and-host-bridge.md](plans/03-builtins-modules-and-host-bridge.md) | `fs` / `proc` / `json` / `text` の host bridge と module surface を決める | 01, 02, [`specs/sx-language/plans/03-type-system-and-standard-surface.md`](../sx-language/plans/03-type-system-and-standard-surface.md) | agent 用 script が guest 内で実用になる |
| 04 | [plans/04-repl-validation-and-bytecode-handoff.md](plans/04-repl-validation-and-bytecode-handoff.md) | REPL UX、host/QEMU test、bytecode handoff 境界を固める | 01, 02, 03, [`specs/sx-language/plans/04-diagnostics-fixtures-and-compatibility.md`](../sx-language/plans/04-diagnostics-fixtures-and-compatibility.md) | 日常利用の実行経路と将来拡張の境界が固まる |
| 05 | [plans/05-process-io-and-fork-expansion.md](plans/05-process-io-and-fork-expansion.md) | `argv`、env、fd / bytes I/O、path、time、`spawn` / `wait` / `pipe` / `fork`、`list` / `map` / `result` の runtime と guest bridge を固める | 02, 03, 04, [`specs/sx-language/plans/06-system-interop-surface-expansion.md`](../sx-language/plans/06-system-interop-surface-expansion.md) | script から fd / pid / pipe / child process と helper object を実用的に扱える |
| 06 | [plans/06-network-runtime-and-resource-tracking.md](plans/06-network-runtime-and-resource-tracking.md) | `net` namespace、socket resource tracking、host/QEMU の client/server 回帰を固める | 02, 03, 04, 05, [`specs/sx-language/plans/07-network-literals-and-branching-sugar.md`](../sx-language/plans/07-network-literals-and-branching-sugar.md) | `sxi` で socket client / server を継続的に検証できる |

## マイルストーン

| マイルストーン | 対象 plan | 到達状態 |
|---|---|---|
| M0: command surface の固定 | 01 | `sxi` の呼び出し方、loader、shared frontend の配置が固まる |
| M1: evaluator core の固定 | 02 | value と call frame を持つ source interpreter が説明できる |
| M2: host bridge の固定 | 03 | file / process / JSON / text の標準入口がそろう |
| M3: 検証導線と次段の境界固定 | 04 | REPL、agent workflow、host/QEMU test、bytecode handoff がそろう |
| M4: interop runtime の固定 | 05 | `argv`、env、fd / bytes I/O、path、time、`spawn` / `wait` / `pipe` / `fork`、`list` / `map` / `result` が host/QEMU で固定される |
| M5: network runtime の固定 | 06 | `net` namespace と socket cleanup が host/QEMU の client / server 回帰で固定される |

## 現時点の判断

- `sxi` は compiler の代用品ではなく、初期の主実行系として扱う
- language frontend は `sxi` 専用品にせず、`libsx` として切り出す
- process bridge は shell 経由の文字列実行に寄せすぎず、spawn 契約を明示する
- GC より先に arena と reset point を整備する
- `sxb` / `sxc` は後段だが、AST と runtime 境界は今の plan で意識する
- fd と pid を扱う runtime object は固定長 table で先に成立させる

## 実装順序

1. `sxi` の CLI と shared frontend 組み込み位置を固定する
2. evaluator core を host unit test 前提で作る
3. builtin host bridge を file / process / JSON / text の順で足す
4. REPL、`--check`、agent workflow を固める
5. その後に bytecode handoff と `sxb` / `sxc` への拡張点を整理する

## 主な設計判断

- command 名は `/usr/bin/sxi`
- v0 は source interpreter、v1 以降で bytecode VM を視野に入れる
- runtime error は fail-fast を基本にし、recoverable error 機構は後段候補とする
- REPL の長寿命状態は session arena と `:reset` で制御する
- process API は `execve()` の現行契約に合わせつつ、`spawn` を主 API に据え、`fork` は明示的な低水準操作として扱う
- socket API は raw fd を返しつつ、runtime 側で追跡して cleanup を担保する

## source loader / stdlib placement

- relative import は current file 基準で解決する
- plain import path は current file の directory を先に探し、見つからなければ `/usr/lib/sx` を探す
- stdlib module は `/usr/lib/sx/<path>.sx` に置き、guest sample と host fixture から同じ path で import できる
- loader は cycle を拒否し、同じ file は source tree に 1 回だけ取り込む

## session lifetime / limits / cleanup

- `sxi file.sx` と `sxi -e` は毎回 fresh runtime を作り、終了時に dispose する
- REPL は session を持続し、`:reset` で binding / scope / call stack を初期化する
- `sx_runtime_reset_session()` は argv、output callback、limit 設定を保持したまま runtime state を消す
- reset / dispose 時には tracked pipe と socket を閉じ、GC なしでも fd leak を残さない
- default limit は `SX_MAX_BINDINGS`、`SX_MAX_SCOPE_DEPTH`、`SX_MAX_CALL_DEPTH`、`max_loop_iterations=1024` とする
- host runtime test で custom binding / loop / call limit と reset 後の resource cleanup を継続確認する

## validation matrix

- host unit test は lexer / parser / runtime / CLI / fixture corpus をカバーする
- representative fixture corpus は `tests/test_sx_fixtures.c` で回し、stdin 付き grep-lite まで同じ経路で検証する
- guest/QEMU smoke は `make -C src test-qemu-sxi` で representative sample を実行し、guest 内 minimal `httpd` の HTTP 応答まで確認する
- agent workflow smoke は `make -C src test-qemu-sxi-agent` で回し、`write_file` -> `sxi --check` -> `sxi run` -> fix loop を mock Claude 経由で検証する

## shared boundary for `sxb` / `sxc`

- `sx_common.h` は language version、frontend ABI、runtime ABI、固定長 limit を定義する
- `sx_parser.h` は token / AST / diagnostic span を共有し、`libsx` frontend の唯一の入口とする
- `sx_runtime.h` は tagged value、runtime limit、check / execute / reset の boundary を定義する
- `sxb` / `sxc` はこの source contract と builtin namespace surface を保ったまま backend だけ差し替える前提にする

## 未解決論点

- REPL session arena と loaded module cache を分離 cache にするか
- `proc` namespace の API を `argv` 指定中心にするか、shell convenience を許すか
- runtime stack trace をどこまで詳細に出すか
- file / JSON API の戻り値を fail-fast に寄せるか、structured error を早めに持つか
- `sxb` 用の bytecode IR を AST 直後に作るか、evaluator 専用の lowering を別に持つか
- `fork()` を raw に expose しつつ、`spawn` / `pipe` 主体の script style をどう推奨するか
- socket handle を raw fd のままにするか、v1 で抽象化するか

## 関連 spec

- [`specs/sx-language/README.md`](../sx-language/README.md)
- [`specs/sxc-compiler/README.md`](../sxc-compiler/README.md)
- [`specs/agent-filesystem-tools/README.md`](../agent-filesystem-tools/README.md)
- [`specs/shell-and-init/README.md`](../shell-and-init/README.md)
- [`specs/memory-scaling/README.md`](../memory-scaling/README.md)
