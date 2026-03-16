# Plan 03: Shared SSH Core

## 概要

現在の `src/net/ssh_server.c` は、
I/O、state、packet codec、auth、crypto 呼び出しが 1 つの file に寄っている。
これを userland `sshd` でそのまま使うのは無理があるため、
shared core を先に切り出す。

## 分離対象

- packet encode / decode
- `string`, `name-list`, `uint32` helper
- `KEXINIT` negotiation
- `password` auth policy
- channel state machine
- session close / exit-status の pure logic

## I/O 依存として残すもの

- socket read / write
- `PTY` read / write
- audit 出力
- process spawn
- config 読み込み
- RNG seed と host key material の取得

## 実装方針

- `src/lib/ssh/` のような shared directory を追加する
- kernel 版と userland 版は同じ core を別 adapter で包む
- まず host test を足してから kernel 版の call site を置き換える
- cutover 完了まで kernel `ssh_server.c` も shared core を使う

## 期待する効果

- protocol bug を host test で先に検知できる
- userland `sshd` を作る前に分離リスクを小さくできる
- 将来 `debug shell` や別 transport を足す時も state machine を流用しやすい

## 完了条件

- `src/net/ssh_server.c` の大半が adapter 層になる
- packet / auth / channel の host test が追加される
- 同じ core を userland build から link できる
