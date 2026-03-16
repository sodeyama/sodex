# feature-userland-ssh-server セッションメモ

更新日: 2026-03-16
対象ブランチ: `feature/userland-ssh-server`

## 現在地

- userland sshd への OpenSSH 接続は `password` login、`wrong password`、`reconnect` を含めて `src/test/run_qemu_ssh_smoke.py` で通る。
- `channel open`、`pty-req`、`shell`、`exit` まで通り、serial では `ssh_session_start` と `ssh_close reason=shell_exit` を確認済み。
- 残件は feature 完了後の cleanup と audit 整理が中心。

## ここまでの結論

- host signer の 2 本目 reply 不達は、signer request ごとの UDP socket 開閉と userland `recvfrom` 直読みに戻すことで解消した。
- `SSH_MSG_NEWKEYS` 後で止まっていた本当の主因は、network ではなく scheduler だった。
- run queue は `init -> term -> eshell -> sshd -> init` だが、timer handler が `TASK_INTERRUPTIBLE` task を先に 1 個進めてから `schedule()` でもう一度 `next` を選んでおり、sleep する `eshell` の直後にいる `sshd` が継続的に飛ばされていた。
- `src/process.c` の double-skip を外すと `sshd` が正常に timeslice を得て、`NEWKEYS` 後の auth / session まで継続して進むようになった。

## 直近の観測

### 解決後の smoke

`src/test/run_qemu_ssh_smoke.py` は以下を 1 run で通す。

- `ssh_success_session()` で login
- `ssh_wrong_password()` で auth failure
- 再度 `ssh_success_session()` で reconnect

`build/log/ssh_serial.log`

- `AUDIT ssh_newkeys_rx`
- `AUDIT ssh_auth_success peer=10.0.2.2`
- `AUDIT ssh_auth_failure peer=10.0.2.2`
- `AUDIT ssh_channel_open_ok`
- `AUDIT ssh_session_start peer=10.0.2.2 pid=4`
- `AUDIT ssh_session_start peer=10.0.2.2 pid=5`
- `AUDIT ssh_close peer=10.0.2.2 reason=shell_exit`

## 現在の主な変更点

- `src/net/ssh_server.c`
  - userland 側 `socket_try_accept()` を `accept_nowait()` ベースに変更。
  - `rxbuf_read_direct()` は事前 poll を外し、`recv_msg` / `recvfrom` を直接呼ぶ形に変更。
  - `ssh_pump_rx()` は `ssh_socket_rx_ready()` を使う 256 byte chunk 読みの形に整理。
  - `ssh_signer_recv_exact()` は userland で `kern_recvfrom()` を直接呼ぶ形に変更。
  - signer request ごとに UDP socket を開閉する形へ戻した。
  - userland wait は blocking `poll` 依存を避け、non-blocking readiness 判定 + `sleep_ticks(1)` に寄せた。
- `src/process.c`
  - timer interrupt で `TASK_INTERRUPTIBLE` / `TASK_ZOMBIE` task を事前に進めていた処理を外し、sleeping task の直後を二重に飛ばさないようにした。
- `src/socket.c`
  - UDP remote port `10026` 向けの targeted audit を追加。
  - `UDP: SEND` / `UDP: RECV` / `UDP: DROP` を必要最小限だけ出す。
- `src/test/run_qemu_ssh_smoke.py`
  - raw banner probe `wait_for_ssh_banner()` の main からの呼び出しは外したまま。
  - signer 起動を含む smoke のまま使っている。
- `src/usr/lib/libc/i386/get_kernel_tick.S`
  - userland 側の tick 取得 syscall wrapper を追加。

## 再現コマンド

build:

```sh
make -C src SODEX_ROOTFS_OVERLAY=../build/log/ssh-rootfs-overlay all
```

smoke:

```sh
SODEX_SSH_PORT=10022 \
SODEX_SSH_PASSWORD=mypass \
SODEX_SSH_SIGNER_PORT=10026 \
SODEX_SSH_EXPECT_TIMEOUT=90 \
python3 src/test/run_qemu_ssh_smoke.py build/bin/fsboot.bin build/log
```

停止:

```sh
pkill -9 -f 'qemu-system-i386|run_qemu_ssh_smoke.py|tests/ssh_signer' || true
```

## 次にやること

- `src/net/ssh_server.c` と `src/socket.c` に残っている調査用 audit を整理する。
- userland `sshd` と scheduler 修正を前提に、関連 spec / README へ最終状態を反映する。
- reconnect 後の pid 増加や timeout policy など、hardening 系の見直しを別タスクで切る。

## 補足

- `tests/derive_ssh_keypair` と `tests/ssh_signer` は host 側生成物なので commit 対象から外す。
