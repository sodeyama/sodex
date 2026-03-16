# Plan 02: Userland I/O Wait

## 概要

userland `sshd` は listener socket、active socket、`PTY` を同時に扱う。
現在の kernel 実装は timer interrupt 側で polling しているが、
daemon 化では同じやり方をそのまま userland に持ち込まない。

そのため、最小の wait primitive を先に入れる。

## 方針

- API は `poll()` 互換の最小 subset を優先する
- 初期対応 fd は socket と `PTY` のみに絞る
- event は `POLLIN`, `POLLOUT`, `POLLHUP` 程度に絞る
- timeout は tick 単位でよい

## 必要な作業

1. kernel 側で `poll` syscall を追加する
2. socket / `PTY` ごとに ready 判定を出せるようにする
3. userland header と libc wrapper を追加する
4. busy loop しない sample daemon で挙動を確認する
5. host test と QEMU smoke で timeout / wakeup / hangup を固定する

## 割り切り

- `select()` は後回し
- `O_NONBLOCK` 全面対応も後回し
- 初期は少数 fd 前提でよい
- `poll()` は `sshd` だけでなく `debug shell` の userland 化にも再利用する

## 完了条件

- listener socket を block せず待てる
- active socket と `PTY` の両方を単一 loop で待てる
- disconnect と shell exit を `POLLHUP` 相当で検知できる
- idle 時に spin しない
