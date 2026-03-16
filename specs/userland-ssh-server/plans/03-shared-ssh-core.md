# Plan 03: Shared SSH Core

## 概要

現在の `src/net/ssh_server.c` は、
I/O、state、packet codec、auth、crypto 呼び出しが 1 つの file に寄っている。
これを userland `sshd` でそのまま使うのは無理があるため、
shared core を先に切り出す。

2026-03-16 の再調査では、OpenSSH が `auth2.c`、`channels.c`、`audit.c`
など責務単位で file を分け、OpenBSD `sshd_config(5)` でも
`sshd-auth` / `sshd-session` を別 binary として扱えることを確認した。
この plan でも、kernel / userland の build 切替ではなく、責務分割で進める。

## 実装結果

- `src/lib/ssh_packet_core.c` / `src/lib/ssh_auth_core.c` /
  `src/lib/ssh_channel_core.c` を追加し、packet / auth / channel の pure logic を分離した
- `src/net/ssh_server.c` は shared core を呼ぶ adapter / compat 中心に寄せ、
  `src/usr/command/sshd.c` から同じ core を userland build で使う形にした
- host test として `tests/test_ssh_packet.c`、`tests/test_ssh_auth.c`、
  `tests/test_ssh_channel.c` を追加し、OpenSSH の `none` auth probe も
  `protocol_error` ではなく `USERAUTH_FAILURE` へ落ちることを確認した

## 分離方針

shared core は次の 3 層に分ける。

1. `transport/codec`
2. `auth`
3. `channel/session`

socket / `PTY` / audit / config / RNG / hostkey / clock / process spawn は
adapter に残す。

## 分離対象と現行関数

### 1. `transport/codec`

- `ssh_queue_payload()`
- `ssh_try_decode_plain_packet()`
- `ssh_try_decode_encrypted_packet()`
- `ssh_append_rx()`
- writer / reader helper
- `string`, `name-list`, `uint32` helper
- `KEXINIT` negotiation の pure logic

### 2. `auth`

- `ssh_handle_service_request()`
- `ssh_handle_userauth_request()`
- `ssh_handle_auth_failure()`
- `password` auth policy
- auth timeout / retry / username-service pinning

### 3. `channel/session`

- `ssh_handle_channel_open()`
- `ssh_handle_channel_request()`
- `ssh_handle_channel_data()`
- peer window 減算
- session close / `exit-status` / `EOF` / `CLOSE` の順序

### 4. adapter に残すもの

- listener create / accept / send / recv
- socket ready 判定
- `PTY` alloc / read / write / winsize / foreground signal
- shell spawn / exit poll
- audit 出力
- config 読み込み
- RNG seed と host key material の取得
- userland `main()` loop と wait

## 実装順

1. packet encode / decode と helper を core へ切り出す
2. auth state machine を core へ切り出す
3. channel / session state machine を core へ切り出す
4. kernel 版 / userland 版の adapter を薄くつなぎ直す
5. `src/net/ssh_server.c` を compat glue 中心に縮める

## file の切り方

- `src/lib/ssh/` のような shared directory を追加する
- 候補:
  - `ssh_packet_core.c`
  - `ssh_auth_core.c`
  - `ssh_channel_core.c`
  - `ssh_server_adapter.h`
- kernel 版と userland 版は同じ core を別 adapter で包む
- cutover 完了まで kernel `ssh_server.c` も shared core を使う

## host test の追加順

1. `tests/test_ssh_packet.c`
   - plain/encrypted packet decode
   - invalid length / invalid padding
   - `name-list` negotiation
2. `tests/test_ssh_auth.c`
   - `ssh-userauth` service accept
   - wrong password
   - retry limit
   - username / service change
3. `tests/test_ssh_channel.c`
   - 単一 `session` channel 制約
   - `pty-req` / `window-change`
   - `exit-status` -> `EOF` -> `CLOSE` の順序

## 進め方の注意

- 分離の単位は `#ifdef USERLAND_SSHD_BUILD` ではなく責務で切る
- `server_runtime_*` と `server_audit_*` の userland 専用 wrapper は core に入れない
- protocol bug を host test で先に検知してから adapter を差し替える
- `debug shell` や将来の別 transport でも state machine を再利用できる形にする

## 完了条件

- `src/net/ssh_server.c` の大半が adapter / compat 層になる
- packet / auth / channel の host test が追加される
- 同じ core を kernel build と userland build から link できる
- `USERLAND_SSHD_BUILD` 専用の大きな ifdef 塊が shared core から消える

上記のうち、shared core 追加、host test 固定、kernel / userland 共通 link は
今回の実装で満たした。`USERLAND_SSHD_BUILD` の runtime wrapper は残るが、
`USS-03` の対象だった pure logic の分離は完了扱いとする。

## 参考

- OpenBSD `sshd_config(5)`: https://man.openbsd.org/sshd_config
- OpenSSH portable: https://github.com/openssh/openssh-portable
- RFC 4252: https://www.rfc-editor.org/rfc/rfc4252
- RFC 4254: https://www.rfc-editor.org/rfc/rfc4254.html
