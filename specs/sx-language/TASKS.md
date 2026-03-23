# SX Language Tasks

`specs/sx-language/README.md` を、着手単位へ落としたタスクリスト。
`sx` の source 契約を先に固定し、`sxi` と将来の `sxc` が
同じ frontend を共有できる状態まで持っていく。

## 進捗メモ

- 2026-03-22: `sx` 用の独立 spec を新設し、`sxi` runtime spec と分離した
- 2026-03-22: 調査結果をもとに、interpreter first だが compiler を閉ざさない language plan として再整理した
- 2026-03-22: initial bring-up として、`let` と `io.print` / `io.println` を持つ最小 frontend を `libsx` と host test / QEMU smoke に載せた
- 2026-03-22: `fn` / `let` / block / `if` / `while` / `return`、`str` / `bool` / `i32`、namespace call、relative `import` を `libsx` + `sxi` で通し、host test と QEMU smoke を更新した
- 2026-03-22: call 引数を expression 化し、nested call を AST / runtime / host test / QEMU smoke で扱えるようにした
- 2026-03-22: guest 同梱の `/home/user/sx-examples/` を追加し、README 付きで hello / import / json / fs / proc の実例を置いた
- 2026-03-22: 次段の表現力拡張として、operator / assignment / `break` / `continue` / `for` を `Plan 05` で進める方針を追加した
- 2026-03-22: operator / assignment / grouping / `for` / `break` / `continue` を parser / runtime / host test / QEMU smoke / sample 群まで通した
- 2026-03-22: 次段の system interop 拡張として、`argv`、fd I/O、path、time、`spawn` / `wait` / `pipe` / `fork` を `Plan 06` で整理した
- 2026-03-22: `argv`、fd I/O、path、time、`spawn` / `wait` / `pipe` / `fork` を language surface / host test / QEMU smoke / guest sample まで通した
- 2026-03-23: `proc.has_env`、`bytes`、`list`、`map`、`result`、`try_*` を sample / fixture / QEMU smoke まで広げ、guest 同梱 corpus を拡充した
- 2026-03-23: 次段として `list` / `map` literal、`else if`、`net` namespace を `Plan 07` で整理した

## 優先順

1. source 契約と corpus
2. grammar / AST
3. semantic contract と builtin surface
4. diagnostics / fixtures / compatibility

## M0: source 契約の固定

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SX-01 | `.sx` file、encoding、comment、line ending、entrypoint の source 契約を固定する | なし | script と module の source 単位が仕様で説明できる |
| [x] | SX-02 | keyword / identifier / literal / operator の lexical rule を定義する | SX-01 | tokenizer が ambiguity なしに切れる |
| [~] | SX-03 | representative corpus と negative corpus を用意し、仕様例と parser fixture を共有する | SX-01, SX-02 | host / QEMU smoke に hello, file copy, control flow, import, invalid source を追加済み。fixture runner の整理は残る |

## M1: grammar / AST

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SX-04 | declaration / statement / expression grammar を v0 scope で固定する | SX-02 | `fn`, `let`, block, `if`, `while`, `return`, call, literal が parse できる |
| [x] | SX-05 | shared frontend 向け AST node、source span、symbol 名の表現を定義する | SX-04 | `sxi` と `sxc` が同じ AST を読める |
| [x] | SX-06 | parser error、incomplete input、recovery point の契約を定義する | SX-04, SX-05 | file 実行と REPL の両方で診断方針がぶれない |

## M2: semantic contract

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SX-07 | primitive type、mutability、scope、name resolution の規則を定義する | SX-05 | `sxi` と `sxc` で同じ束縛規則を使える |
| [x] | SX-08 | builtin namespace と fail-fast / predicate ベースの初期エラー方針を固定する | SX-07 | stdlib surface の入口が language spec 側で説明できる |
| [~] | SX-09 | import / module / visibility / cycle policy を v0 で固定する | SX-04, SX-07, SX-08 | relative `import` と cycle 拒否は実装済み。visibility / export の明文化は残る |

## M3: diagnostics / fixtures / compatibility

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SX-10 | human-readable diagnostic と future JSON diagnostic の共通 source span 契約を定義する | SX-05, SX-06 | `path:line:column` と machine-readable span がそろう |
| [~] | SX-11 | host fixture runner と examples を整理し、hello / file copy / grep-lite / JSON parse を language corpus に追加する | SX-03, SX-08, SX-09 | host / QEMU で hello, file copy, JSON, import は共有済み。fixture runner と grep-lite 例は残る |
| [ ] | SX-12 | `sxi` / `sxc` 共通の versioning と compatibility policy を定義する | SX-07, SX-08, SX-09, SX-10 | language version と runtime / compiler version の関係を説明できる |

## M4: expression / control flow expansion

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SX-13 | unary / binary operator、grouping、assignment の grammar / AST / semantic contract を固定する | SX-04, SX-05, SX-07 | `i = i + 1`、`a && b`、`(x + 1) * 2` が一貫して parse / eval できる |
| [x] | SX-14 | `break` / `continue` / `for` の statement 契約と loop scope を固定する | SX-04, SX-05, SX-07, SX-13 | counter loop と loop 制御が host / QEMU で再現できる |
| [x] | SX-15 | expanded examples / fixture / smoke を追加し、guest 同梱 sample を網羅化する | SX-11, SX-13, SX-14 | operator / assignment / loop 制御の sample と回帰がそろう |

## M5: system interop expansion

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SX-16 | `io` / `fs` / `time` namespace の fd / path / cwd / sleep 契約を固定する | SX-08, SX-10, SX-13 | stdin/stdout/fd I/O、path 操作、cwd、time API が言語 surface として説明できる |
| [x] | SX-17 | `proc` namespace の `argv` / `spawn` / `wait` / `pipe` / `fork` / `exit` 契約を固定する | SX-08, SX-10, SX-13, SX-14 | pid / fd / child process を扱う script 契約が一貫して説明できる |
| [x] | SX-18 | interop 系 example / fixture / smoke を追加し、guest sample を網羅化する | SX-11, SX-15, SX-16, SX-17 | `argv` / path / spawn / pipe / fork / time / env / bytes / list / map / result の sample と回帰がそろう |

## M6: literal / network expansion

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | SX-19 | `list` / `map` literal と `else if` sugar の grammar / AST / semantic contract を固定する | SX-04, SX-05, SX-07, SX-13 | `[]` / `{}` / `else if` が host / QEMU で一貫して parse / eval できる |
| [ ] | SX-20 | `net` namespace の client / server surface と check mode 契約を固定する | SX-08, SX-10, SX-16, SX-17 | `connect` / `listen` / `accept` / `read` / `write` / `poll_read` / `close` が言語 surface として説明できる |
| [ ] | SX-21 | literal / network 系 example / fixture / smoke を追加し、guest sample を拡充する | SX-11, SX-18, SX-19, SX-20 | collection literal、`else if`、guest client / server の sample と回帰がそろう |

## 先送りする項目

- closures、first-class function、anonymous function
- classes、methods、inheritance
- generics、trait、interface 相当
- optimizer、macro system、package manager
- Unicode identifier、`float`、`i64`
