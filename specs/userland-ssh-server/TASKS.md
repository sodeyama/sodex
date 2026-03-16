# Userland SSH Server Tasks

`specs/userland-ssh-server/README.md` を、着手単位に落としたタスクリスト。
最初は kernel listener を残したまま並走実装し、最後に切り替える。

## 進捗メモ

- 2026-03-16 時点で `/usr/bin/sshd`、`poll` syscall、`get_foreground_pid` syscall、`init` からの既定起動は入っている
- kernel 常駐の `ssh_server_init()` / `ssh_server_tick()` 呼び出しは既定経路から外し、userland `sshd` へ切り替え済み
- host signer の 2 本目 reply を読めず KEX で止まる問題は、signer request ごとの UDP socket 開閉と userland `recvfrom` 直読みに寄せて解消した
- `SSH_MSG_NEWKEYS` 後で止まっていた主因は scheduler の double-skip で、`TASK_INTERRUPTIBLE` task の直後にいる `sshd` が starvation していた
- 2026-03-16 の再調査で signer-less regression を確認したが、`ssh_signer_roundtrip()` ベースの単一経路へ寄せることで解消した
- `USERLAND_SSHD_BUILD` でも `ssh_signer_port=0` が既定で通り、`test-qemu-ssh` は signer-less 既定で green に戻った
- `curve25519` shared secret の all-zero check を追加し、KEX failure は `protocol_error` 一括ではなく `kexinit_invalid` / `kex_failed` / `newkeys_invalid` へ分けた
- `src/makefile` と `bin/restart.sh` の `ssh_signer_port` 既定値は `0` に揃え、`bin/restart.sh server-headless --ssh` と README 記載の host 側 `ssh` 手順で login / exit を再確認した
- userland `sshd` で password auth failure を 1 接続 3 回までに制限し、close reason を `auth_retry_limit`、timeout reason を `auth_timeout` / `idle_timeout` に分けた
- 外部調査の結論として `/etc/sodex-admin.conf` は当面維持しつつ、`server_runtime_config` / `server_audit` を shared entrypoint にして `ssh` / `debug shell` / `http` の `admin_server` 直参照を減らした
- `test-qemu-ssh` の reconnect 回帰は、userland `recv==0` の close 検出と `uip_conn->appstate` の stale child 切り離しで解消した

## 優先順

1. process model と kernel/userland 契約の固定
2. userland I/O wait primitive
3. `SSH` core の shared 化
4. userland `sshd` の bring-up
5. `PTY` relay と shell lifecycle
6. cutover と hardening
7. regression recovery と signer-less 完全復旧

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
| [x] | USS-05 | userland `sshd` で listener, version exchange, KEX, `password` auth を通す | USS-02, USS-03, USS-04 | signer 経由 KEX と `password` auth が通り、`wrong password` も smoke で確認済み |
| [x] | USS-06 | `session`, `pty-req`, `shell`, `window-change` と `PTY` relay を userland 側で実装する | USS-05 | `PTY` relay, shell 起動, `exit` 後 close, reconnect を smoke で確認済み |

## M2: cutover

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | USS-07 | `server` / `server-headless` の既定起動を userland `sshd` へ切り替える | USS-05, USS-06 | `init` の既定起動と kernel 側 call site の切替は完了した |
| [x] | USS-08 | host test と `test-qemu-ssh` を userland `sshd` 前提で固定する | USS-05, USS-06 | `src/test/run_qemu_ssh_smoke.py` が login / wrong password / reconnect を通す |
| [~] | USS-09 | kernel listener と `ssh_server_tick()` 依存を削り、cleanup する | USS-07, USS-08 | 既定経路の依存削除と green test は完了。調査用 audit / helper の整理は残る |

## M3: 残フォローアップ

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | USS-10 | `ssh_*` config を `admin_server` 依存からさらに分離するか判断する | USS-07 | 外部調査の結果、`/etc/sodex-admin.conf` は当面維持し、shared `server_runtime_config` API を挟む方針で固定した |
| [x] | USS-11 | audit sink を userland server 共通で使える形に整理する | USS-07 | `server_audit` を shared entrypoint とし、`ssh` / `debug shell` / `http` が同じ sink を使う形へ整理した |
| [~] | USS-12 | 単一接続制限を維持したまま、timeout と auth retry 制限を見直す | USS-08 | 1 接続 3 回失敗で切断と timeout reason の分離は実装済み。retry/backoff や閾値の再調整は残る |

## M4: signer-less 復旧と完走条件の再固定

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | USS-13 | userland `sshd` の signer request 経路を local fallback 付きの単一経路へ統一する | USS-07, USS-08 | `USERLAND_SSHD_BUILD` でも `ssh_signer_port=0` で KEX が通り、`bin/restart.sh server-headless --ssh` 単体で password prompt まで進む |
| [x] | USS-14 | KEX helper の責務を整理し、最低限の protocol gap を埋める | USS-13 | `curve25519` shared secret の all-zero check を追加し、signer を使う場合も使わない場合も同じ KEX hash / sign path を通る |
| [x] | USS-15 | KEX / transport failure の audit と close reason を分離する | USS-13, USS-14 | `protocol_error` 一括ではなく `kex_failed_*` などで識別でき、必要なら `SSH_MSG_DISCONNECT` reason code を返せる |
| [x] | USS-16 | signer mode の起動導線、smoke、README を現実の挙動と一致させる | USS-13 | 既定は signer-less で通り、signer 付きは opt-in として `start.sh` / `restart.sh` / smoke / README / spec の期待値が一致する |
| [x] | USS-17 | userland `sshd` の最終受け入れ条件を再度 green に固定する | USS-13, USS-14, USS-15, USS-16 | `test-qemu-ssh` が signer-less 既定で login / wrong password / reconnect を通し、manual でも README 記載の host 側 `ssh` 手順で login / exit が成立する |
