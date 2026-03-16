# Plan 01: Process Model と Kernel Contract

## 概要

`SSH server` を userland へ移す前に、何を kernel に残し、
何を daemon 側へ渡すかを固定する。
ここが曖昧なまま code を動かし始めると、
config / audit / 起動導線 / fallback が全部ぶれる。

## 決めること

- daemon 名は `/usr/bin/sshd` にする
- 初期は `server` / `server-headless` 時だけ起動する
- config は当面 `/etc/sodex-admin.conf` の `ssh_*` をそのまま読む
- `SSH` transport / auth / channel / shell relay は userland の責務にする
- kernel は socket / `PTY` / process / signal / wait primitive の提供に寄せる
- kernel listener は移行完了まで feature flag 付き fallback として残す

## kernel に残す責務

- TCP socket 実装
- `accept` / `send` / `recv` / `close`
- `TTY` / `PTY`
- `execve_pty()`
- signal / `waitpid()`
- userland が使う wait primitive

## userland へ移す責務

- banner / packet parser
- KEX / key material 展開
- `password` auth
- channel state machine
- `PTY` relay
- shell 起動と cleanup
- runtime config の `SSH` 部分読み込み

## 起動モデル

初期は supervisor を増やさず、`init` から直接 `sshd` を起動する。
`term` と同じく、最小の child process として追加する。

段階的には次で進める。

1. manual 起動できる `sshd` を作る
2. `server-headless` だけ自動起動へ寄せる
3. `server` でも既定起動へ広げる

## audit / log 方針

最初は大掛かりな logging 基盤を増やさない。
次のどちらかで固定する。

1. kernel 側 audit sink に line を追加する最小 API を出す
2. `sshd` の stderr/serial 出力を audit source として扱う

初期は 1 を優先する。
既存 `server_runtime_ready` や `ssh_auth_failure` の見え方を崩しにくいから。

## 完了条件

- daemon の責務と kernel の責務が文章で固定される
- config 互換維持の方針が決まる
- audit の初期経路が決まる
- fallback をどの phase まで残すか決まる
