# Userland SSH Server Tasks

`specs/userland-ssh-server/README.md` を、着手単位に落としたタスクリスト。
最初は kernel listener を残したまま並走実装し、最後に切り替える。

## 進捗メモ

- 2026-03-16 時点で `/usr/bin/sshd`、`poll` syscall、`get_foreground_pid` syscall、`init` からの既定起動は入っている
- kernel 常駐の `ssh_server_init()` / `ssh_server_tick()` 呼び出しは既定経路から外し、userland `sshd` へ切り替え済み
- 未解決は `test-qemu-ssh` の安定化で、初回 session は通る run がある一方、`wrong password` と reconnect が teardown / reaccept で不安定

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
| [~] | USS-01 | userland `sshd` の責務、起動条件、config/audit 境界を固定する | なし | `/usr/bin/sshd` の起動経路と config snapshot / audit sink は実装済み。責務分割を spec 側へさらに反映する余地がある |
| [x] | USS-02 | socket + `PTY` を待つ最小 wait API を追加する | USS-01 | `poll` syscall と `get_foreground_pid` syscall を追加し、listener / session socket / `PTY` を userland から待てる |
| [~] | USS-03 | `src/net/ssh_server.c` から pure logic を shared core へ分離する | USS-01, USS-02 | userland build は `USERLAND_SSHD_BUILD` で成立したが、pure logic の独立と host test 化は未完 |

## M1: userland `sshd` の bring-up

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | USS-04 | `/usr/bin/sshd` の build / image 収録 / manual 起動経路を追加する | USS-01, USS-03 | `src/usr/command/sshd.c` を追加し、image 収録と `init` からの起動経路を入れた |
| [~] | USS-05 | userland `sshd` で listener, version exchange, KEX, `password` auth を通す | USS-02, USS-03, USS-04 | listener / banner / KEX / auth は初回 session で到達する run があるが、`wrong password` を含む安定化が未完 |
| [~] | USS-06 | `session`, `pty-req`, `shell`, `window-change` と `PTY` relay を userland 側で実装する | USS-05 | `eshell` 起動と `PTY` relay は userland 化済み。`exit` 後の clean close と reconnect の整合が未完 |

## M2: cutover

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | USS-07 | `server` / `server-headless` の既定起動を userland `sshd` へ切り替える | USS-05, USS-06 | `init` の既定起動と kernel 側 call site の切替は完了した |
| [~] | USS-08 | host test と `test-qemu-ssh` を userland `sshd` 前提で固定する | USS-05, USS-06 | smoke script は userland 前提へ更新したが、login / wrong password / reconnect の全緑は未達 |
| [~] | USS-09 | kernel listener と `ssh_server_tick()` 依存を削り、cleanup する | USS-07, USS-08 | 既定経路の依存削除は入ったが、関連 cleanup と green test までは未完 |

## M3: 残フォローアップ

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | USS-10 | `ssh_*` config を `admin_server` 依存からさらに分離するか判断する | USS-07 | `/etc/sodex-admin.conf` 継続か、専用 config file へ分離するかを決める |
| [ ] | USS-11 | audit sink を userland server 共通で使える形に整理する | USS-07 | `debug shell` や将来の daemon と共通の監査出力方針を持てる |
| [ ] | USS-12 | 単一接続制限を維持したまま、timeout と auth retry 制限を見直す | USS-08 | userland 化後も現在の制限と hardening を落とさない |
