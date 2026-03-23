# SX Language Spec

`sx` は、Sodex 内で agent と人間が書く主言語の spec。
この spec は source language 自体を扱い、実行系は
[`specs/sxi-runtime/`](../sxi-runtime/README.md) に分離する。

初期判断の前提は、次の調査に置く。

- [guest native programming environment research](../../doc/research/sodex_guest_native_programming_environment_research_2026-03-22.md)
- [guest runtime interpreter research](../../doc/research/sodex_guest_runtime_interpreter_research_2026-03-22.md)

## 背景

`sx` を独立 spec として切る理由は、`sxi` と将来の `sxc` が
同じ source 契約を共有できるようにするためである。
現状の repo にはすでに shell、`vi`、agent、file system、ELF 実行基盤があるが、
そのまま shell を主言語にすると次の制約が強い。

- `agent` の `write_file` は 1 回 3072 bytes 上限
- `agent` の `run_command` は `sh -c` 経由で約 10 秒 timeout
- 既存 shell は script 実行まで育っているが、`SHELL_STORAGE_SIZE` や
  `SHELL_MAX_NODES` が小さく、値型も文字列中心
- userland libc は最小構成で、`execve()` は Unix 的な自己置換ではなく
  spawn 的な契約を持つ
- guest 側メモリは既定 512MB、最大 1GB まで使える

関連箇所:

- [`README.md`](../../README.md)
- [`src/usr/lib/libagent/tool_run_command.c`](../../src/usr/lib/libagent/tool_run_command.c)
- [`src/usr/lib/libagent/tool_write_file.c`](../../src/usr/lib/libagent/tool_write_file.c)
- [`src/usr/include/shell.h`](../../src/usr/include/shell.h)
- [`src/execve.c`](../../src/execve.c)
- [`specs/memory-scaling/README.md`](../memory-scaling/README.md)

このため、言語は次を満たす必要がある。

- agent が短い差分で書きやすい
- parser と診断が deterministic である
- `sxi` と `sxc` が同じ frontend を共有できる
- 初手から GC、複雑な object model、最適化器を要求しない

## ゴール

- `sx` の source 形式、字句規則、文法、AST 契約を固定する
- `sxi` と将来の `sxc` が共有する semantic contract を定義する
- agent 向け script と、小さな module 群の両方を書ける形にする
- 診断形式、fixture corpus、代表サンプルを仕様に含める
- v0 を host test と QEMU smoke で継続検証できるようにする

## 非ゴール

- C 互換または POSIX shell 互換を目指さない
- 初回から class、継承、closure、thread、fiber、macro を入れない
- 最初から generics、optimizer、bytecode、self-host を同時に入れない
- `float`、`i64`、FFI 全量、package manager を v0 完了条件にしない
- runtime sandbox や capability policy を `sx` だけで完結させない

## 目標アーキテクチャ

```text
source (.sx, UTF-8)
   │
   ▼
lexer
   │
   ▼
parser
   │
   ▼
AST / symbol / diagnostics
   │
   ├─ sxi (source interpreter)
   └─ sxc (future native compiler)
```

共通 frontend は新しい `libsx` として切り出し、
`sxi` と `sxc` から共有する前提で設計する。

## v0 の設計方針

- source は UTF-8 とし、comments / strings は UTF-8 を通す
- v0 の keyword と identifier は ASCII に寄せ、正規化問題を避ける
- grammar は brace block と semicolon 終端を基本にして parse を単純化する
- script 向けに top-level 実行を許しつつ、module 契約は別途固定する
- primitive type は最小から始め、container と error model は保守的に切る
- implicit conversion は極力入れず、`bool` 判定も明示に寄せる

## Plans

| # | ファイル | 概要 | 依存 | この plan の出口 |
|---|---|---|---|---|
| 01 | [plans/01-source-contract-and-language-goals.md](plans/01-source-contract-and-language-goals.md) | source file、entrypoint、字句規則、reserved word を固定する | なし | `sx` の source 契約と corpus が固まる |
| 02 | [plans/02-grammar-and-ast.md](plans/02-grammar-and-ast.md) | declaration / statement / expression grammar と AST を定義する | 01 | `libsx` frontend の parse 出力が安定する |
| 03 | [plans/03-type-system-and-standard-surface.md](plans/03-type-system-and-standard-surface.md) | type、scope、mutability、builtin namespace、module surface を決める | 01, 02 | `sxi` と `sxc` が共有できる semantic contract が固まる |
| 04 | [plans/04-diagnostics-fixtures-and-compatibility.md](plans/04-diagnostics-fixtures-and-compatibility.md) | 診断形式、fixture、examples、versioning を固定する | 01, 02, 03 | 実装前から回帰基準と互換ポリシーを持てる |
| 05 | [plans/05-expression-and-control-flow-expansion.md](plans/05-expression-and-control-flow-expansion.md) | operator、assignment、`break` / `continue`、`for` を追加し、手続き表現を広げる | 02, 03, 04 | 実用的な loop / 条件 / 更新が host/QEMU で固定される |
| 06 | [plans/06-system-interop-surface-expansion.md](plans/06-system-interop-surface-expansion.md) | `argv`、env、fd / bytes I/O、path、time、`spawn` / `wait` / `pipe` / `fork`、`list` / `map` / `result` を含む system interop surface を固定する | 03, 04, 05 | script から file / process / pipe / child process と回復可能な helper 値を一通り扱える契約が固まる |
| 07 | [plans/07-network-literals-and-branching-sugar.md](plans/07-network-literals-and-branching-sugar.md) | `list` / `map` literal、`else if` sugar、`net` namespace を追加し、script の表現力を広げる | 02, 03, 04, 05, 06 | collection literal と client/server network script を言語契約として固定できる |

## マイルストーン

| マイルストーン | 対象 plan | 到達状態 |
|---|---|---|
| M0: source 契約の固定 | 01 | `.sx` file、lexical rule、entrypoint が一貫して説明できる |
| M1: grammar / AST の固定 | 02 | declaration / statement / expression と AST node が安定する |
| M2: semantic contract の固定 | 03 | type と builtin namespace の責務が `sxi` / `sxc` 共通で説明できる |
| M3: 回帰基準の固定 | 04 | diagnostics、fixtures、examples、互換ポリシーがそろう |
| M4: 表現力拡張 | 05 | operator、assignment、`break` / `continue`、`for` が言語契約として固定される |
| M5: system interop 拡張 | 06 | `argv`、env、fd / bytes I/O、path、time、`spawn` / `wait` / `pipe` / `fork`、`list` / `map` / `result` の surface が言語契約として固定される |
| M6: literal / network 拡張 | 07 | `list` / `map` literal、`else if`、`net` namespace が host/QEMU 前提で説明できる |

## 現時点の判断

- 初手の `sx` は「C 風の見た目だが C 互換ではない」言語として切る
- 既存 shell は glue 用に残し、`sx` を主言語にする
- `sx` の frontend は `sxi` に埋め込み、後で `sxc` に流用する
- v0 は source interpreter に優しい language に寄せる
- 失敗時の体験は「曖昧に動く」より「はっきり fail する」を優先する

## 実装順序

1. source 契約、lexical rule、sample corpus を固定する
2. grammar と AST を `libsx` 前提で切り出す
3. type / builtin / module contract を `sxi` 実装可能な最小集合に絞る
4. diagnostics と examples を host test 前提で揃える
5. その後に [`specs/sxi-runtime/`](../sxi-runtime/README.md) を実装へ下ろす

## 主な設計判断

- `sx` は interpreter first だが、compiler を閉ざさない構文にする
- source は UTF-8、identifier は v0 では ASCII に制限する
- expression の暗黙変換は避ける
- 複雑な型機構より、読みやすい grammar と明快な runtime contract を先に固める
- error handling は language 機能と stdlib 契約を分けて考える
- process / fd API は shell 文字列ではなく argv / fd / pid を中心に広げる
- network API は `io` に混ぜず、socket を明示する `net` namespace に分ける

## v0 module / visibility policy

- module 境界は file 単位とし、`import "path";` は loader directive として扱う
- import path は absolute path、relative path、plain path を受け付ける
- plain path は current file の directory を先に探し、見つからなければ `/usr/lib/sx` を探す
- stdlib module は `/usr/lib/sx/<path>.sx` に置き、`import "std/strings";` のように参照する
- v0 には `export` / `private` を入れず、import 済み file の top-level function はすべて visible とする
- imported file の top-level statement は merged source tree の順で 1 回だけ評価する
- import cycle は reject し、duplicate function 名や同一 scope の duplicate `let` も診断で止める

## corpus / regression set

- host fixture runner は `tests/test_sx_fixtures.c` を基準にし、stdin 付き sample まで同じ経路で回す
- valid runtime corpus は recursive sum、relative import、stdlib import、spawn / wait、bytes / result、list / map、literal / branching、grep-lite、minimal `httpd` を含む
- negative corpus は undefined name と import cycle を持ち、`--check` 診断の基準にする
- guest 同梱 corpus は `/home/user/sx-examples/` に置き、README と LANGUAGE.md と QEMU smoke で共有する

## versioning / compatibility

- source language version は `SX_LANGUAGE_VERSION` で管理し、現行は `0.1.0` とする
- lexer / parser / AST 契約は `SX_FRONTEND_ABI_VERSION`、runtime bridge 契約は `SX_RUNTIME_ABI_VERSION` で追跡する
- `sxi --version` は language version と frontend/runtime ABI version を表示する
- v0 source file に version pragma は持たず、repo 単位で 1 つの language version を運用する
- breaking な grammar / semantic 変更は language version を上げ、sample / fixture / smoke / spec を同時更新する
- `sxc` / `sxb` は source language version を中心に互換性を宣言し、個別の tool version だけでは互換を表さない

## 未解決論点

- v1 以降で `export` / `private` を syntax として持つか
- top-level statement を v1 でどこまで絞るか
- `list` / `map` の element typing を v0 で持つか
- recoverable error を generic な `Result` 型で持つか、fail-fast + predicate に寄せるか
- 将来 `sxc` が要求する ABI 情報を AST / semantic layer にどこまで持たせるか
- `fork` を fail-fast language surface として expose するか、より高水準の `spawn` を主 API に置くか
- `map` literal の key を v0 で string 限定にするか、identifier shorthand まで許すか
- source file 自身に version pragma を持たせるか

## 関連 spec

- [`specs/sxi-runtime/README.md`](../sxi-runtime/README.md)
- [`specs/sxc-compiler/README.md`](../sxc-compiler/README.md)
- [`specs/agent-filesystem-tools/README.md`](../agent-filesystem-tools/README.md)
- [`specs/shell-and-init/README.md`](../shell-and-init/README.md)
- [`specs/memory-scaling/README.md`](../memory-scaling/README.md)
