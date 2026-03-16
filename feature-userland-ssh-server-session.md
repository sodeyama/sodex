# feature-userland-ssh-server セッションメモ

更新日: 2026-03-16
対象ブランチ: `feature/userland-ssh-server`

## 現在地

- userland sshd は `accept`、banner、client `KEXINIT`、curve25519 の signer request/reply までは通る。
- 本命の未解決は、hostkey signature request の reply を guest 側が読めていないこと。
- password prompt の前で止まり、`src/test/run_qemu_ssh_smoke.py` の expect が timeout する。

## ここまでの結論

- 問題は `accept` や banner ではない。
- `ssh_kex_hash_done` の後に投げる hostkey signature request までは host signer に届く。
- host signer は reply を返しているが、guest 側では 2 回目の `ssh_signer_read_len` が出ずに止まる。

## 直近の観測

### 長めに進んだ run で見えたこと

`build/log/ssh_signer_stderr.log`

- `accept signer request`
- `curve25519 reply ok`
- `accept signer request`
- `signer reply ok`

`build/log/ssh_serial.log`

- `AUDIT ssh_kex_sign mode=kex_start port=10026`
- `AUDIT ssh_kex_client_public_ok`
- `AUDIT ssh_signer_read_len=68`
- `AUDIT ssh_kex_curve_done`
- `AUDIT ssh_kex_hostkey_blob_done`
- `AUDIT ssh_kex_hash_done`
- `AUDIT ssh_kex_sign mode=remote port=10026`

この時点で 2 回目の `ssh_signer_read_len` が出ず、そのまま止まる。

### targeted UDP audit を入れた直近 run

`build/log/ssh_serial.log`

- `UDP: SEND sockfd=4 lport=1025 rport=10026 len=68`
- `UDP: RECV sockfd=4 lport=1025 rport=10026 len=68`

これは curve25519 の 1 本目の request/reply で、2 本目まではまだ観測し切れていない。

## 現在の主な変更点

- `src/net/ssh_server.c`
  - userland 側 `socket_try_accept()` を `accept_nowait()` ベースに変更。
  - `rxbuf_read_direct()` は事前 poll を外し、`recv_msg` / `recvfrom` を直接呼ぶ形に変更。
  - `ssh_pump_rx()` は `ssh_socket_rx_ready()` を使う 256 byte chunk 読みの形に整理。
  - `ssh_signer_recv_exact()` は userland で `poll(fd, POLLIN | POLLHUP, 1)` 後に `kern_recv()` する。
  - signer 用 fd は `ssh_signer_fd` の 1 本に寄せた。
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
python3 src/test/run_qemu_ssh_smoke.py build/bin/fsboot.bin build/log
```

停止:

```sh
pkill -9 -f 'qemu-system-i386|run_qemu_ssh_smoke.py|tests/ssh_signer' || true
```

## 次にやること

- 2 本目の signer request が guest から本当に送信されているかを UDP audit で確定する。
- 送信済みなら、`ssh_signer_recv_exact()` の poll/recv 条件と fd 状態遷移をさらに絞る。
- 未送信なら、`ssh_kex_hash_done` 後の request 組み立てから追う。

## 補足

- `tests/derive_ssh_keypair` と `tests/ssh_signer` は host 側生成物なので commit 対象から外す。
