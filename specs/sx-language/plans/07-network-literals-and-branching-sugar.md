# Plan 07: Network / Literal / Branching Sugar Expansion

## 目的

`sx` に `list` / `map` literal、`else if` sugar、`net` namespace を追加し、
script と小さな service を shell へ戻らずに書ける段まで押し上げる。

## 背景

`sx` はすでに file / process / pipe / fork まで扱えるが、次の不足が残っている。

- collection 初期化が `list.new()` / `map.new()` から始まるため、短い script が冗長
- 条件分岐が `else { if (...) { ... } }` になり、意図が見えづらい
- network client / server を書く surface が無く、socket を使う用途で shell や C に戻る

Sodex 自体は socket / poll / DNS 基盤を既に持つため、言語 surface を先に固定する価値がある。

## 今回の scope

### 1. `list` literal

- `[]`
- `[expr]`
- `[expr, expr, ...]`

生成される値は既存の `list` handle と同じ runtime object とする。

### 2. `map` literal

- `{}`
- `{"name": expr}`
- `{"name": expr, "ok": expr, ...}`

v0 の key は string literal のみに制限する。
値は任意 expr を許す。

### 3. `else if` sugar

```sx
if (a) {
  io.println("a");
} else if (b) {
  io.println("b");
} else {
  io.println("c");
}
```

parser では nested `if` へ desugar してよい。
runtime からは通常の `if` / `else` と同じに見えることを契約とする。

### 4. `net`

- `net.connect(host, port) -> i32`
- `net.listen(port) -> i32`
- `net.accept(listener) -> i32`
- `net.read(sock) -> str`
- `net.read_bytes(sock) -> bytes`
- `net.write(sock, text) -> i32`
- `net.write_bytes(sock, data) -> i32`
- `net.poll_read(sock, timeout_ticks) -> bool`
- `net.close(sock) -> bool`

初期実装は TCP / blocking I/O に絞る。
`host` は dotted IPv4 を必須、guest 実装では DNS 解決も許す。

## 契約方針

### 1. literal と builtin の関係

- `[]` は `list.new()` と `list.push()` を順に適用した結果と等価
- `{}` は `map.new()` と `map.set()` を順に適用した結果と等価
- equality は既存の handle identity を維持する

### 2. `net` の値モデル

- socket は `i32` として表現する
- `io.read_fd` / `io.write_fd` と混同しないよう、基本操作は `net.*` に閉じる
- runtime は socket を追跡し、session reset / dispose 時に close できるようにする

### 3. check mode

`--check` では side effect を起こさず、`net.*` は dummy socket と空 payload を返す。
これにより type / control flow の検査だけは維持する。

## 非ゴール

- UDP
- TLS
- socket option 全量
- async / event loop
- `map` の identifier shorthand

## テスト方針

### host unit test

- `[]` / `{}` / nested literal の parse
- `else if` の parse
- literal 実行結果
- `net.connect` / `net.read` / `net.write`
- `net.listen` / `net.accept` / `net.poll_read`

### QEMU smoke

- guest client が host server へ接続する script
- guest server が host client を受ける script
- literal / `else if` sample の `--check` と実行

## 変更対象

- `specs/sx-language/README.md`
- `specs/sx-language/TASKS.md`
- `specs/sxi-runtime/README.md`
- `specs/sxi-runtime/TASKS.md`
- `src/usr/include/sx_lexer.h`
- `src/usr/include/sx_parser.h`
- `src/usr/include/sx_runtime.h`
- `src/usr/lib/libsx/lexer.c`
- `src/usr/lib/libsx/parser.c`
- `src/usr/lib/libsx/runtime.c`
- `tests/test_sx_lexer.c`
- `tests/test_sx_parser.c`
- `tests/test_sxi_runtime.c`
- `src/test/run_qemu_sxi_smoke.py`
- `src/rootfs-overlay/home/user/sx-examples/`
