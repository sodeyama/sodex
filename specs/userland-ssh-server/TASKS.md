# Userland SSH Server Tasks

`specs/userland-ssh-server/README.md` を、着手単位に落としたタスクリスト。
最初は kernel listener を残したまま並走実装し、最後に切り替える。

## 優先順

1. process model と kernel/userland 契約の固定
2. userland I/O wait primitive
3. `SSH` core の shared 化
4. userland `sshd` の bring-up
5. `PTY` relay と shell lifecycle
6. cutover と hardening

## M0: 境界整理

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | USS-01 | userland `sshd` の責務、起動条件、config/audit 境界を固定する | なし | `init` / `start.sh` / overlay がどこまで責任を持つか文章で固定される |
| [ ] | USS-02 | socket + `PTY` を待つ最小 wait API を追加する | USS-01 | userland daemon が busy loop なしで listener / session socket / `PTY` を待てる |
| [ ] | USS-03 | `src/net/ssh_server.c` から pure logic を shared core へ分離する | USS-01, USS-02 | packet / auth / channel state が host test 可能な形で userland からも使える |

## M1: userland `sshd` の bring-up

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | USS-04 | `/usr/bin/sshd` の build / image 収録 / manual 起動経路を追加する | USS-01, USS-03 | guest 内で `sshd` バイナリを起動できる |
| [ ] | USS-05 | userland `sshd` で listener, version exchange, KEX, `password` auth を通す | USS-02, USS-03, USS-04 | host `ssh` client と handshake し、auth success/failure まで動く |
| [ ] | USS-06 | `session`, `pty-req`, `shell`, `window-change` と `PTY` relay を userland 側で実装する | USS-05 | login 後に `eshell` が動き、`exit` と disconnect まで整合する |

## M2: cutover

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | USS-07 | `server` / `server-headless` の既定起動を userland `sshd` へ切り替える | USS-05, USS-06 | 起動導線は維持したまま userland 実装が既定になる |
| [ ] | USS-08 | host test と `test-qemu-ssh` を userland `sshd` 前提で固定する | USS-05, USS-06 | login / wrong password / reconnect / client disconnect の回帰が自動化される |
| [ ] | USS-09 | kernel listener と `ssh_server_tick()` 依存を削り、cleanup する | USS-07, USS-08 | kernel 側 SSH 常駐処理が不要になる |

## M3: 残フォローアップ

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | USS-10 | `ssh_*` config を `admin_server` 依存からさらに分離するか判断する | USS-07 | `/etc/sodex-admin.conf` 継続か、専用 config file へ分離するかを決める |
| [ ] | USS-11 | audit sink を userland server 共通で使える形に整理する | USS-07 | `debug shell` や将来の daemon と共通の監査出力方針を持てる |
| [ ] | USS-12 | 単一接続制限を維持したまま、timeout と auth retry 制限を見直す | USS-08 | userland 化後も現在の制限と hardening を落とさない |
