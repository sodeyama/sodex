# SX Language Tasks

`specs/sx-language/README.md` を、着手単位へ落としたタスクリスト。
`sx` の source 契約を先に固定し、`sxi` と将来の `sxc` が
同じ frontend を共有できる状態まで持っていく。

## 進捗メモ

- 2026-03-22: `sx` 用の独立 spec を新設し、`sxi` runtime spec と分離した
- 2026-03-22: 調査結果をもとに、interpreter first だが compiler を閉ざさない language plan として再整理した
- 2026-03-22: initial bring-up として、`let` と `io.print` / `io.println` を持つ最小 frontend を `libsx` と host test / QEMU smoke に載せた
- 2026-03-22: `fn` / `let` / block / `if` / `while` / `return`、`str` / `bool` / `i32`、namespace call、relative `import` を `libsx` + `sxi` で通し、host test と QEMU smoke を更新した

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

## 先送りする項目

- closures、first-class function、anonymous function
- classes、methods、inheritance
- generics、trait、interface 相当
- optimizer、macro system、package manager
- Unicode identifier、`float`、`i64`
