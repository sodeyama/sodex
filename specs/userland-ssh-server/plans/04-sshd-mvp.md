# Plan 04: Userland `sshd` MVP

## 概要

shared core と wait API が揃ったら、userland `sshd` を最小構成で起動する。
ここでは listener、transport、auth までを first milestone とする。

## 初期スコープ

- `ssh_port` で listen
- 1 active connection
- `curve25519-sha256` / `ssh-ed25519` / `aes128-ctr` / `hmac-sha2-256`
- `password` auth
- user 名 `root` 固定
- `CHANNEL_OPEN session` は 1 本だけ受ける

## 後段へ回すもの

- `PTY` relay
- `shell` request の実処理
- `window-change`
- shell exit cleanup

## 作業項目

1. `/usr/bin/sshd` を build 対象に追加する
2. `/etc/sodex-admin.conf` の `ssh_*` を userland で読めるようにする
3. listener を作り、accept loop を userland で回す
4. banner, `KEXINIT`, `NEWKEYS`, `password` auth を通す
5. auth success / failure を audit に残す
6. `session` までは受けるが、`shell` request はいったん failure で返せるようにする

## 完了条件

- guest 内で `sshd` を単独起動できる
- host の `ssh` client が handshake できる
- `wrong password` を拒否できる
- `ps` に `sshd` が見える
