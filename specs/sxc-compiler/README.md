# SXC Compiler Spec

`sxc` は `sx` source を Sodex userland ELF へ AOT compile する compiler。
言語契約は [`specs/sx-language/`](../sx-language/README.md)、
interpreter 実行系は [`specs/sxi-runtime/`](../sxi-runtime/README.md) で扱い、
この spec は native backend と build/run 導線を扱う。

## 背景

`sxi` により `sx` の language surface と host bridge は実動段階に入った。
一方で、長寿命 service を常駐させる段では次の不足が残る。

- 起動ごとに interpreter を挟むため、service 常駐のオーバーヘッドが大きい
- init / service 管理へ載せる単位としては、単体 ELF の方が扱いやすい
- `sx` で書いた program を userland の通常 binary と同じ経路で配布できない

このため `sxc` v0 は、言語全量より先に
「guest 内で `httpd.sx` を compile し、実際に起動して host から叩ける」
ことを受け入れ基準に置く。

関連箇所:

- [`specs/sx-language/README.md`](../sx-language/README.md)
- [`specs/sxi-runtime/README.md`](../sxi-runtime/README.md)
- [`src/usr/makefile.inc`](../../src/usr/makefile.inc)
- [`src/usr/lib/crt0.S`](../../src/usr/lib/crt0.S)
- [`src/rootfs-overlay/home/user/sx-examples/httpd.sx`](../../src/rootfs-overlay/home/user/sx-examples/httpd.sx)
- [`src/test/run_qemu_sxi_smoke.py`](../../src/test/run_qemu_sxi_smoke.py)

## ゴール

- `/usr/bin/sxc` を guest userland command として追加する
- `libsx` frontend を `sxi` と共有し、lexer / parser / AST / diagnostic の二重実装を避ける
- v0 では `httpd.sx` が使う subset を native compile できる
- 既存 userland ABI と link 手順に合わせて i486 ELF を出力できる
- compile 済み `httpd` を guest で起動し、host 側 `curl` で `200 OK` を確認できる
- host unit test と QEMU smoke で `sxi` / `sxc` の representative parity を継続確認できる

## 非ゴール

- 初手から full language parity を完了すること
- self-host compiler、optimizer、SSA、debugger を同時に作ること
- closure、class、generic など未導入機能を先回りで設計すること
- 外部 assembler / linker 依存を guest 側必須にすること

## 既存前提

- userland ELF は `ld -m elf_i386 -nostdlib` 前提で link している
- program entry は `crt0.o` を通し、libc / syscall wrapper を静的 link する
- `libsx` は既に shared frontend として `sxi` 側へ切り出されている
- `httpd.sx` は `sxi` と `test-qemu-sxi` で実動確認済みで、compiler 側の受け入れ sample に使える

## v0 の compiler 方針

- driver 名は `/usr/bin/sxc` とする
- frontend は `libsx` をそのまま使い、parse / check 契約は `sxi` とそろえる
- backend は `httpd` 受け入れに必要な subset から始める
- builtins は interpreter と同じ namespace 名を保ちつつ、compile 時は runtime helper 呼び出しへ lower する
- v0 は最適化より正しさと ABI 一致を優先する
- output は直接実行可能な userland ELF とし、`sxc file.sx -o /usr/bin/app` を成立させる

## `httpd` 受け入れ subset

最低限、次を compile できる必要がある。

- `fn`、`let`、`if`、`while`、`return`
- `str`、`bool`、`i32`
- 代入、比較、算術、`&&` / `||` / `!`
- builtin namespace call
- string literal と `\r\n` escape

v0 の受け入れ sample は [`httpd.sx`](../../src/rootfs-overlay/home/user/sx-examples/httpd.sx) とし、
`GET /healthz`、`GET /`、`404` 応答が guest 上で再現できることを完了条件に含める。

## フェーズ

| フェーズ | 概要 | 出口 |
|---|---|---|
| P0 | driver と shared frontend の再利用 | `sxc --check` が `sxi --check` と同じ診断契約を返せる |
| P1 | `httpd` subset の semantic check / lowering | `httpd.sx` を backend 入力へ安定して落とせる |
| P2 | runtime helper と i486 ELF 出力 | `hello.sx` 相当を native ELF として実行できる |
| P3 | guest `/usr/bin/sxc` と compiled `httpd` smoke | host から compiled `httpd` に HTTP request を投げて応答を確認できる |

## 実装順序

1. `sxc --check` と frontend 共有境界を固定する
2. `httpd.sx` が使う subset 専用の lowering / type 前提を固める
3. builtin を呼ぶための runtime helper 境界を切る
4. i486 userland ELF 出力を最小構成で成立させる
5. QEMU で compiled `httpd` を起動し、host request まで smoke 化する
6. その後に対象 subset を広げる

## 主な設計判断

- `sxc` の成功条件は「理論上 compile できる」ではなく「guest で起動して host から叩ける」とする
- `sxi` と `sxc` は syntax / diagnostic / builtin 名を共有し、backend 差分だけを許す
- compiler runtime helper は `libsx` 本体と分離し、interpreter 専用 state を link しない
- userland ABI は既存 C toolchain が出す呼び出し規約に合わせる
- `httpd.sx` を acceptance target に固定し、language 拡張と compiler 実装の scope creep を防ぐ

## 未解決論点

- v0 backend を直接 ELF writer にするか、内部 object 形式を 1 段持つか
- builtin 呼び出しを個別 helper に落とすか、薄い runtime dispatch を残すか
- string / bytes / list / map の所有権を compiler runtime でどう持つか
- guest 上の `sxc` を最初から実用速度にするか、まず host test で backend を固めるか
- `sxi` と `sxc` の parity corpus をどこまで同一 fixture で共有するか

## 関連 spec

- [`specs/sx-language/README.md`](../sx-language/README.md)
- [`specs/sxi-runtime/README.md`](../sxi-runtime/README.md)
- [`specs/server-runtime/README.md`](../server-runtime/README.md)
