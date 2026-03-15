# Plan 08: SSH Server の段階導入

## 概要

Plan 07 の TCP-PTY bridge で server 側 `PTY` / session relay を固定した後に、
暗号化を含む最小の `SSH server` を段階的に入れる。
目的は最初から完全な `sshd` 互換を目指すことではなく、`OpenSSH` client から接続できる最小実装を作ること。

## 最初の到達点

最初の milestone は次の 1 本に絞る。

- host の `OpenSSH` client から `ssh -p <hostfwd_port> root@127.0.0.1` で guest `eshell` に入れる
- 認証後は 1 接続につき 1 `session` channel、1 `PTY`、1 shell のみを扱う
- `scp`, `sftp`, port forwarding, agent forwarding, `exec` request は初期スコープ外として reject する

この段階で狙うのは「通常の `ssh` で shell に入れる」ことであり、
`sshd` の多機能互換ではない。

## スコープ

### 初期

- `SSHv2` のみ
- listener は `default off`
- `OpenSSH` client と相互接続できる最小の algorithm profile ひとつ
- 認証方式は 1 つだけ
- 単一 active connection と単一 `session` channel
- `pty-req`, `shell`, `window-change`, `exit-status`
- allowlist / audit / timeout / close は既存 server runtime と同じ制約を維持

### 後回し

- `scp`, `sftp`
- port forwarding
- agent forwarding
- `exec`, `env`, `subsystem`
- 複数 auth method
- 複数 algorithm suite
- 複数 channel / 複数同時 session
- guest 内での host key 自動生成や永続化 UI
- 公開 internet 向け hardening 一式

## 設計判断

- 先に Plan 07 で `PTY` relay を固定し、暗号化と shell relay を分離して進める
- 既存の `allowlist` / audit / connection limit を外さない
- host key がなければ fail closed にする
- 最初は guest 内で鍵生成しない。host 側から seed/material を注入する
- crypto は最初の 1 suite に絞り、相互接続性と実装量の両方を優先する
- crypto primitive は large dependency を丸ごと抱えず、最小 API で vendored 実装を包む方針を優先する
- shell 以外の subsystem は初期スコープに入れない
- packet parser と channel state machine は shell relay から独立した層として切る

## 初期 profile の固定方針

最初に目指す on-wire profile は次の組み合わせとする。

- KEX: `curve25519-sha256`
- server host key: `ssh-ed25519`
- cipher: `aes128-ctr`
- MAC: `hmac-sha2-256`
- compression: `none`
- userauth: `password`

この profile を最初に選ぶ理由:

- host 側の `OpenSSH` client が素直に話せる
- `password` auth なら user public key 管理を後回しにできる
- transport は AEAD ではなく `CTR + HMAC` に留めることで packet 層を分離しやすい

初期 auth policy は次で固定する。

- user 名は `root` 固定
- password は `ssh_password` config で注入する
- `ssh_password` 未設定時は listener を起動しない

listener / secret / host key の初期注入は既存 config parser に寄せる。

- `ssh_port=<port>`
- `ssh_password=<secret>`
- `ssh_hostkey_ed25519_seed=<hex>`
- `ssh_rng_seed=<hex>`

host key と RNG seed は最初から OpenSSH private key format を読まない。
guest 側 parser を軽く保つため、初期実装では固定長 hex seed を読む。

## 状態遷移

最低限の state machine は次の順に限定する。

1. TCP accept
2. version exchange
3. `KEXINIT`
4. key exchange
5. `NEWKEYS`
6. `SERVICE_REQUEST ssh-userauth`
7. `USERAUTH_REQUEST password`
8. `SERVICE_REQUEST ssh-connection`
9. `CHANNEL_OPEN session`
10. `CHANNEL_REQUEST pty-req`
11. `CHANNEL_REQUEST shell`
12. `CHANNEL_DATA` / `CHANNEL_WINDOW_ADJUST` / `window-change`
13. `CHANNEL_EOF` / `CHANNEL_CLOSE`

初期実装で受ける request は絞る。

- 受ける: `pty-req`, `shell`, `window-change`
- 拒否する: `exec`, `env`, `subsystem`, forwarding 系 request, 複数 channel

## 実装フェーズ

### Phase 0: scope と profile を固定する

1. listener を `ssh_port` default off で追加する
2. 初期 algorithm profile を 1 本に固定する
3. 初期 auth を `password` のみに固定する
4. scope 外 request の reject 方針を固定する

### Phase 1: crypto と seed 注入の土台を作る

1. `src/lib/crypto/` を追加し、vendored primitive を収める
2. `X25519`, `Ed25519`, `SHA-256`, `SHA-512`, `AES-CTR`, `HMAC-SHA256` の最小 wrapper API を定義する
3. `ssh_hostkey_ed25519_seed` と `ssh_rng_seed` を config から読む
4. seed から packet padding, ephemeral key, IV に使う DRBG を作る
5. seed 未設定時 fail closed にする

### Phase 2: transport packet 層を実装する

1. version banner の送受信
2. SSH binary packet の encode / decode
3. `uint32`, `string`, `name-list`, `mpint` helper
4. packet size 上限、padding、disconnect reason
5. preauth state machine

### Phase 3: key exchange と `NEWKEYS`

1. `KEXINIT` negotiation を 1 profile に絞って実装する
2. `curve25519-sha256` の exchange hash と shared secret 導出
3. `ssh-ed25519` host key public key 生成と署名
4. key material expansion
5. `aes128-ctr` / `hmac-sha2-256` で encrypt/decrypt を有効化する

### Phase 4: userauth を実装する

1. `ssh-userauth` service を受ける
2. `password` auth のみ受ける
3. `root` 固定 user と `ssh_password` を照合する
4. auth 失敗と retry を audit に残す
5. allowlist / timeout / close と接続制限を統合する

### Phase 5: `session` channel と `PTY` をつなぐ

1. `ssh-connection` service を受ける
2. `CHANNEL_OPEN session` を 1 本だけ許可する
3. `pty-req` で winsize を初期化する
4. `shell` request で `openpty()` / `execve_pty()` に接続する
5. `CHANNEL_DATA` と `PTY` relay を双方向に回す
6. `window-change` を `tty_set_winsize()` に反映する
7. shell 終了時に `exit-status`, `EOF`, `CLOSE` を返す

### Phase 6: smoke / docs / manual operation を揃える

1. host test を packet, KEX, auth, channel に分けて追加する
2. `run_qemu_ssh_smoke.py` を追加する
3. hostfwd, known_hosts, password を含む manual 手順を書く
4. `PF:` や handshake failure を即 fail にする

## テスト戦略

### host test

- packet codec
- name-list negotiation
- `KEXINIT` parser
- key loader / seed parser
- auth policy
- channel state machine

### QEMU smoke

- ready marker まで boot
- `ssh` client で login
- `pwd` / `echo` / `exit` で shell relay を確認
- 誤 password の拒否
- reconnect
- `window-change` の反映

### 手動確認

- `ssh -p <host_port> root@127.0.0.1`
- `ssh -tt -p <host_port> root@127.0.0.1`
- `ssh -o StrictHostKeyChecking=yes -o UserKnownHostsFile=<tmpfile> ...`

## 主なリスク

- guest 内で信頼できる entropy source をまだ持っていない
- crypto primitive を複数まとめて導入する必要がある
- OpenSSH private key format まで読むと parser が一気に重くなる
- shell relay の既存 cleanup と channel close の整合が崩れやすい

初期は次でリスクを抑える。

- host key は seed 注入だけにする
- auth は `password` のみに絞る
- 1 channel / 1 shell / 1 connection に固定する
- scope 外 request は全部 reject する

## 変更対象

- `src/net/ssh_server.c`
- `src/include/ssh_server.h`
- `src/lib/crypto/*`
- `src/net/admin_server.c`
- `src/include/admin_server.h`
- `src/kernel.c`
- `src/tty/tty.c`
- `src/execve.c`
- `src/test/run_qemu_ssh_smoke.py`
- `src/test/write_server_runtime_overlay.py`
- `tests/test_ssh_packet.c`
- `tests/test_ssh_kex.c`
- `tests/test_ssh_auth.c`
- `tests/test_ssh_channel.c`

## 完了条件

- [ ] `OpenSSH` client から追加オプションなしで login できる
- [ ] host key fingerprint が boot を跨いでも固定される
- [ ] `password` auth, `session`, `pty-req`, `shell`, `window-change` が成立する
- [ ] shell 接続の切断と再接続が安定する
- [ ] `exec`, `subsystem`, forwarding, 複数 channel を reject できる
