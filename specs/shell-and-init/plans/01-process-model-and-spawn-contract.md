# Plan 01: Process Model と Spawn Contract

## 概要

shell script と init/service を成立させる前に、
`spawn`, `exec`, `wait`, `signal`, child reap の契約を整理する。

ここが曖昧だと、`sh`, `init`, `service helper`, `sshd` のすべてが
別の前提で process を扱ってしまう。

## 問題

現状の `execve()` は Unix の `exec` ではなく、
新しい child task を作って PID を返す spawn 的な API になっている。
一方で `fork()` は stub で、`waitpid()` も最小である。

このままだと次が崩れる。

- shell built-in `exec`
- background child の既知 pid 管理
- `init` の child reap
- pipeline の multi-child 実行
- daemon の detach / session 分離

## 方針

- shell / init から見える API は、少なくとも次を分ける
  - 親を置換する `exec`
  - 子を増やす `spawn`
- 初期は full `fork()` よりも、`posix_spawn()` に近い API を優先する
- `waitpid(-1)` と `WNOHANG` を先に入れ、`init` と shell の reaper を成立させる
- `kill(pid, 0)` 相当の存在確認も status 用に検討する

## 決めること

1. 既存 `execve()` をどう段階移行するか
2. `spawnve()` / `posix_spawnp()` 相当を追加するか
3. `waitpid()` の拡張範囲
4. `SIGCHLD` を入れるか、polling reap で始めるか
5. process group / session / `setsid()` をどの phase で入れるか

## 推奨案

- まず spawn 系 API を明示する
  - 現在の spawn 的 `execve()` を内部で再利用してもよい
- userland shell / init / service helper は spawn 系 API を使う
- `execve()` の自己置換化は後方互換を見ながら段階移行する
- daemon 用 detach は `setsid()` 互換または `POSIX_SPAWN_SETSID` 風 flag で扱う

この形なら、`fork()` 完装前でも
script 実行と service 起動を先に進めやすい。

## 変更対象

- 既存
  - `src/execve.c`
  - `src/process.c`
  - `src/signal.c`
  - `src/syscall.c`
  - `src/include/execve.h`
  - `src/usr/include/stdlib.h`
  - `src/usr/lib/libc/i386/execve.S`
  - `src/usr/lib/libc/i386/fork.S`
  - `src/usr/lib/libc/i386/waitpid.S`
- 新規候補
  - `src/include/spawn.h`
  - `src/usr/include/spawn.h`
  - `src/usr/lib/libc/i386/spawn.S`

## 検証

- spawn した child を `waitpid(-1, WNOHANG)` で回収できる
- child reaper を持つ `init` が zombie を溜めない
- service helper が pid 存在確認と stop/status を返せる

## 完了条件

- spawn/exec/wait の契約が文書と API で固定される
- `init` と shell の child reap が実装可能になる
- daemon detach に必要な session/process-group 追加位置が決まる
