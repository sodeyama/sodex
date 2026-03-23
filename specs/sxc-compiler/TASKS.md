# SXC Compiler Tasks

`specs/sxc-compiler/README.md` を、実装順へ落としたタスクリスト。
`sx` language spec と `sxi` runtime 実装を前提に、
compiled `httpd` を guest で実動させる順に進める。

## 進捗メモ

- 2026-03-23: `sxc` spec と task を新設し、`httpd.sx` を v0 acceptance target に固定した
- 2026-03-23: `sxi` 側では minimal `httpd` sample と host/QEMU 回帰が通り、compiler の比較対象ができた

## 優先順

1. shared frontend 再利用と driver
2. lowering / runtime helper 境界
3. native ELF 出力
4. guest smoke と parity

## M0: driver / frontend 共有

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | SXC-01 | `/usr/bin/sxc` の CLI 契約を定義し、`--check` / `-o` / entrypoint を固定する | `specs/sx-language/README.md`, `specs/sxi-runtime/README.md` | `sxc --check file.sx` と `sxc file.sx -o app` の使い方が文書で説明できる |
| [ ] | SXC-02 | `libsx` frontend の shared boundary を `sxi` と共通化し、diagnostic parity 基準を定義する | SXC-01 | parser / AST / diagnostic の重複実装を避けられる |

## M1: `httpd` subset lowering

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | SXC-03 | `httpd.sx` が使う subset の semantic 前提と lowering 単位を定義する | SXC-02, `src/rootfs-overlay/home/user/sx-examples/httpd.sx` | `fn` / `let` / `if` / `while` / builtin call を backend 入力へ落とせる |
| [ ] | SXC-04 | builtin namespace call を compiler runtime helper 呼び出しへ lower する境界を決める | SXC-03, `src/usr/makefile.inc` | `text.*` / `net.*` / `io.*` / `proc.*` が native backend から呼べる |

## M2: native backend

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | SXC-05 | i486 userland ABI に合わせた codegen / frame layout / call convention を定義する | SXC-03, SXC-04, `src/usr/lib/crt0.S` | compiled function が既存 libc / syscall wrapper と連携できる |
| [ ] | SXC-06 | userland ELF 出力と link 手順を固め、最小 sample を native 実行する | SXC-05 | `hello.sx` 相当を ELF 化して guest で起動できる |

## M3: parity / smoke

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | SXC-07 | host unit test と fixture で `sxi` / `sxc` parity を確認する | SXC-02, SXC-03, SXC-04, SXC-05, SXC-06 | representative sample の check / 実行結果差分を継続確認できる |
| [ ] | SXC-08 | guest `/usr/bin/sxc` を同梱し、compiled `httpd` の QEMU smoke を追加する | SXC-06, SXC-07 | host から compiled `httpd` に request を投げて `200 OK` を確認できる |

## 先送りする項目

- self-host compiler
- optimizer、debug info、source-level debugger
- full language parity
- package manager、incremental build cache
