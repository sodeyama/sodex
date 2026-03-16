# Plan 05: PTY Relay と Session Lifecycle

## 概要

Plan 04 の `sshd` に shell session をつなぐ。
ここで初めて `openpty()` / `execve_pty()` / `eshell` 起動を userland 側へ寄せる。

## やること

1. `pty-req` を受けて winsize を保持する
2. `shell` request で `openpty()` し、`/usr/bin/eshell` を起動する
3. socket -> `PTY`、`PTY` -> socket の relay を userland loop で回す
4. `window-change` を `tty_set_winsize()` 相当へ反映する
5. `Ctrl-C`, EOF, client disconnect, shell exit を整合させる
6. `exit-status`, `EOF`, `CLOSE` を userland から返す

## 実装上の注意

- shell 起動前に auth/channel state が確定していること
- relay loop は `poll()` 前提で実装すること
- shell exit 時の cleanup と socket close 順序を明示すること
- 初期は 1 shell / 1 channel 固定を維持すること

## テスト

- `pwd`
- `ls`
- `cat` + `Ctrl-C`
- `exit`
- client disconnect
- reconnect

## 完了条件

- host の `ssh -tt` から `eshell` に入れる
- prompt 復帰と clean close が維持される
- `make test-qemu-ssh` 相当の smoke が userland `sshd` で通る
