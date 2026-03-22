# SXI Runtime Tasks

`specs/sxi-runtime/README.md` を、着手単位へ落としたタスクリスト。
`sx` language spec を前提に、guest 内 source interpreter を
最短で成立させる順に進める。

## 進捗メモ

- 2026-03-22: `sxi` 用の独立 spec を新設し、`sx` language spec と責務を分離した
- 2026-03-22: tree-walk interpreter を v0 とし、bytecode / native compiler は後段へ回す方針を固定した
- 2026-03-22: initial bring-up として、`/usr/bin/sxi`、`--check`、`-e`、簡易 REPL、host test、QEMU smoke `test-qemu-sxi` を追加した
- 2026-03-22: `libsx` 共有 frontend、relative `import` loader、`io` / `fs` / `proc` / `json` / `text` builtin、runtime stack trace、host / QEMU smoke を追加した

## 優先順

1. command surface と shared frontend 組み込み
2. evaluator / memory / runtime error
3. builtin host bridge
4. REPL / validation / bytecode handoff

## M0: command surface

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SXI-01 | `/usr/bin/sxi` の CLI 契約を固定し、`file`, `-e`, `--check`, REPL の入口を定義する | `specs/sx-language/plans/01-source-contract-and-language-goals.md` | 実行モードごとの引数と exit code が文書で説明できる |
| [x] | SXI-02 | `libsx` の build / link 配置を決め、`sxi` と将来の `sxc` で共有できるようにする | SXI-01, `specs/sx-language/plans/02-grammar-and-ast.md` | frontend の重複実装を避けられる |
| [~] | SXI-03 | source loader、module search path、stdlib module 配置を固定する | SXI-01, SXI-02, `specs/sx-language/plans/03-type-system-and-standard-surface.md` | relative `import` loader と cycle 拒否は実装済み。stdlib module 配置の整理は残る |

## M1: evaluator / memory

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SXI-04 | tagged value、environment、call frame の runtime 表現を定義する | SXI-02, `specs/sx-language/plans/03-type-system-and-standard-surface.md` | evaluator core の in-memory model が固定される |
| [ ] | SXI-05 | script arena、session arena、recursion / frame limit、resource cleanup 方針を定義する | SXI-04 | GC なしでも寿命管理を説明できる |
| [x] | SXI-06 | tree-walk evaluator と runtime error / stack trace 契約を固める | SXI-04, SXI-05, `specs/sx-language/plans/04-diagnostics-fixtures-and-compatibility.md` | parse 成功後の実行と failure path が host test で固定できる |

## M2: builtin host bridge

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SXI-07 | `io` / `fs` namespace の最小 surface と current directory 契約を定義する | SXI-03, SXI-06 | file read/write と console 出力の入口が固まる |
| [x] | SXI-08 | `proc` / `json` / `text` namespace を定義し、spawn 的 `execve()` と既存 JSON 実装に合わせる | SXI-03, SXI-06, SXI-07 | agent 用 script の実用 surface がそろう |
| [x] | SXI-09 | builtin failure と runtime failure の境界、fail-fast policy、predicate helper を固定する | SXI-06, SXI-07, SXI-08 | script の失敗モードが一貫する |

## M3: REPL / validation / handoff

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SXI-10 | REPL command、multi-line input、`:reset` / `:load` / `:quit` の UX を定義する | SXI-01, SXI-05, SXI-06 | 長時間 session と寿命管理を両立できる |
| [~] | SXI-11 | host unit test、fixture runner、QEMU smoke、agent workflow smoke を追加する | SXI-06, SXI-07, SXI-08, SXI-10 | host / QEMU smoke は追加済み。agent workflow 専用 smoke と fixture runner の整理は残る |
| [ ] | SXI-12 | `sxb` / `sxc` へ渡す shared boundary を定義し、bytecode handoff の準備をする | SXI-02, SXI-06, SXI-11 | v0 実装が後続 bytecode / compiler を塞がない |

## 先送りする項目

- GC、JIT、debugger
- network stdlib
- external package manager
- structured exception / generic `Result`
- concurrency、fiber、async I/O
