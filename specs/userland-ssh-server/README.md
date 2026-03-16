# Userland SSH Server Spec

現在の `SSH` 実装は `src/net/ssh_server.c` にあり、タイマ割り込み経由で
`ssh_server_tick()` が常駐処理を回している。
この方式は最小実装としては機能したが、`ps` から見えず、責務も
「socket listener / protocol parser / crypto / auth / PTY relay」が
kernel 側へ寄り過ぎている。

この spec では、`SSH server` を userland daemon として再設計し、
kernel は socket / `PTY` / process / wait primitive の提供に寄せる。

## 背景

- 現状の listener は process ではなく kernel 常駐ロジックなので `ps` に出ない
- `SSH` の packet parser、暗号、認証、session state が timer interrupt 側にあり、切り分けとデバッグが重い
- 現在の実装は 1 接続 1 session に絞ることで成立しているが、将来の拡張や障害切り分けには userland 化した方が都合がよい
- `debug shell` や将来の他 server も含め、protocol server は userland へ寄せる方が一貫する

## ゴール

- `ps` で見える `/usr/bin/sshd` を持つ
- `SSH` transport / auth / channel / `PTY` relay の主処理を userland へ移す
- kernel 側は socket, `PTY`, signal, scheduler, minimal wait API の提供に絞る
- 現在の最小 profile
  - `curve25519-sha256`
  - `ssh-ed25519`
  - `aes128-ctr`
  - `hmac-sha2-256`
  - `password`
  を維持したまま userland へ移せる
- `make test-qemu-ssh` 相当の回帰を、cutover 後も維持する

## 非ゴール

- いきなり `sshd` 互換全体を目指さない
- `scp`, `sftp`, port forwarding, publickey auth, multi-user を初期スコープに入れない
- kernel 側 socket 実装を全面的に作り直すこと自体は主目的にしない
- daemon 化と同時に複数接続対応まで広げない

## 現状整理

### 既にあるもの

- userland から使える `socket` / `bind` / `listen` / `accept` / `send_msg` / `recv_msg`
- `openpty()` / `execve_pty()` と `eshell` 起動経路
- `waitpid()` と signal の最小基盤
- `SSH` の最小 transport / auth / `PTY` relay 実装
- QEMU smoke (`make test-qemu-ssh`)

### まだ足りないもの

- socket と `PTY` を同時に待つ userland 向け wait primitive
- userland daemon の起動経路
- `SSH` config / audit の責務分離
- kernel 専用実装になっている `SSH` core の分離
- cutover 中の feature flag と fallback

## 目標アーキテクチャ

```text
init
  ├─ /usr/bin/term
  └─ /usr/bin/sshd
       ├─ /etc/sodex-admin.conf の SSH 設定を読む
       ├─ listen/accept
       ├─ SSH transport / auth / channel state machine
       └─ openpty + execve_pty("/usr/bin/eshell")

kernel
  ├─ network_poll / socket 実装
  ├─ PTY / TTY
  ├─ process / signal / wait
  └─ userland 向け最小 wait API
```

初期 cutover では config 名と on-wire profile を変えない。
つまり `/etc/sodex-admin.conf` の `ssh_*` 設定をそのまま読み、
起動導線も `server` / `server-headless` を維持したまま実装を差し替える。

## 設計原則

- timer interrupt 内で protocol を進めない
- pure logic は shared core として切り出し、host test しやすくする
- 初期は 1 daemon / 1 active connection / 1 session channel に固定する
- I/O multiplexing は ad-hoc busy loop ではなく、最小の wait API を定義して行う
- kernel listener は最後まで fallback として残し、最後の phase で削る
- config format と smoke 手順は一度に崩さない

## フェーズ

| # | ファイル | 概要 | 主な依存 |
|---|---|---|---|
| 01 | [plans/01-process-model-and-kernel-contract.md](plans/01-process-model-and-kernel-contract.md) | daemon の責務、起動経路、kernel/userland 境界を固定する | なし |
| 02 | [plans/02-userland-io-wait.md](plans/02-userland-io-wait.md) | socket と `PTY` を待つ最小 wait API を追加する | 01 |
| 03 | [plans/03-shared-ssh-core.md](plans/03-shared-ssh-core.md) | `SSH` core を kernel 専用コードから shared library へ分離する | 01, 02 |
| 04 | [plans/04-sshd-mvp.md](plans/04-sshd-mvp.md) | userland `sshd` の listener, transport, auth を成立させる | 01, 02, 03 |
| 05 | [plans/05-pty-relay-and-session-lifecycle.md](plans/05-pty-relay-and-session-lifecycle.md) | `PTY` relay, shell 起動, close, signal を userland 化する | 04 |
| 06 | [plans/06-cutover-and-hardening.md](plans/06-cutover-and-hardening.md) | 起動切替、smoke parity、kernel listener 削除を行う | 04, 05 |

実装の進捗と残タスクは [TASKS.md](TASKS.md) で管理する。

## 実装順

1. daemon 化の責務と config/audit 境界を決める
2. userland で socket + `PTY` を扱うための wait API を入れる
3. `SSH` packet / auth / channel / crypto wrapper を shared core へ分ける
4. `/usr/bin/sshd` を追加し、listener と auth まで通す
5. `PTY` relay と shell lifecycle を userland 化する
6. smoke を userland daemon 前提へ切り替え、kernel listener を削る

## 主なリスク

- `select` / `poll` 相当が無いままだと daemon が spin しやすい
- `src/net/ssh_server.c` は state と I/O が密結合なので、shared core 分離で一度壊しやすい
- config / audit を急に分離すると既存 smoke と起動 overlay が崩れる
- cutover を一気にやると、原因が kernel か userland か判別しにくい

## 変更候補

- 既存
  - `src/net/ssh_server.c`
  - `src/lib/ssh_crypto.c`
  - `src/include/ssh_server.h`
  - `src/include/admin_server.h`
  - `src/net/admin_server.c`
  - `src/execve.c`
  - `src/usr/init.c`
  - `src/socket.c`
  - `src/syscall.c`
  - `src/usr/include/sys/socket.h`
  - `src/test/run_qemu_ssh_smoke.py`
  - `src/test/write_server_runtime_overlay.py`
- 新規候補
  - `src/usr/command/sshd.c`
  - `src/lib/ssh/*`
  - `src/include/poll.h`
  - `src/usr/include/poll.h`
  - `tests/test_ssh_packet.c`
  - `tests/test_ssh_auth.c`
  - `tests/test_ssh_channel.c`

## 完了条件

- [ ] `ps` に `/usr/bin/sshd` が見える
- [ ] host の `ssh` client から userland `sshd` 経由で `eshell` に入れる
- [ ] `wrong password` / reconnect / `exit` / client disconnect の回帰が維持される
- [ ] kernel 側の `ssh_server_tick()` が不要になる
- [ ] config と smoke 手順が userland 化後も大きく変わらない
