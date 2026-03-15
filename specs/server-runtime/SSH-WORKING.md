# SSH Working Notes

`sodex` の guest 内 SSH server について、実装到達点、運用上の前提、未完了事項を作業用に整理する。
ここは working document であり、最終仕様は `README.md`、`TASKS.md`、`plans/08-ssh-server.md` を優先する。

## 位置づけ

- `sodex` の SSH は `OpenSSH` の `sshd` 互換を目指したものではない
- 現在は `OpenSSH` client から guest `eshell` に入れる最小実装
- 実装場所は主に `src/net/ssh_server.c`
- network 基盤の上に乗る server runtime の一部として扱う

## 現在できること

- host の `OpenSSH` client から guest へ接続できる
- 認証成功後に `PTY` を作り、`/usr/bin/eshell` を起動できる
- 同一 session で `pwd`、`ls`、`cat`、`Ctrl-C`、`exit` を扱える
- wrong password の拒否と reconnect を smoke で確認する前提になっている
- `QEMU user net + hostfwd` で host `127.0.0.1:<host_port>` から guest `10.0.2.15:<guest_port>` へ転送する

## 実装 profile

- SSH version: `SSHv2`
- KEX: `curve25519-sha256`
- server host key: `ssh-ed25519`
- cipher: `aes128-ctr`
- MAC: `hmac-sha2-256`
- compression: `none`
- auth: `password`
- user: `root` 固定

上記 profile は `src/net/ssh_server.c` の `KEXINIT` と auth 処理に固定されている。

## いまの制限

- 1 active connection / 1 session channel 前提
- `exec`、`env`、`subsystem`、forwarding 系 request は初期スコープ外
- `scp`、`sftp`、publickey auth、agent forwarding は未対応
- 多ユーザーや PAM 相当の仕組みはない
- 公開 internet 向け hardening は未完了

## 暗号化通信か

結論として、実装意図は平文 relay ではなく通常の SSH transport である。

- `NEWKEYS` 後に受信側は encrypted packet として decode する
- 送信側は packet に `HMAC-SHA256` を付けて `AES-CTR` で暗号化する
- 受信側は復号後に `HMAC-SHA256` を検証する

したがって、少なくともコード上は「通常の暗号化 SSH 通信」を行う構造になっている。

## 鍵と認証の扱い

### client 側

- 現在の login は `password` auth 前提
- host 側に user の秘密鍵を置く前提ではない
- README の手順でも `PubkeyAuthentication=no` を明示している

### server 側

- server host key は必要
- ただし初期実装は OpenSSH private key format を直接読まない
- `ssh_hostkey_ed25519_seed` か raw public/secret を config から注入する
- `ssh_rng_seed` も config から注入する
- seed 未設定時は fail closed にする方針

要するに、「host に OpenSSH 用の client 秘密鍵は不要」だが、
「server host key material 自体は host 側から build/config に注入している」が正確な整理になる。

## どこから設定されるか

- build-time default は `src/makefile`
- runtime 注入は `/etc/sodex-admin.conf`
- overlay 生成は `src/test/write_server_runtime_overlay.py`
- 起動補助は `bin/start.sh` と `bin/restart.sh`

主な設定項目:

- `ssh_port`
- `ssh_password`
- `ssh_hostkey_ed25519_seed`
- `ssh_hostkey_ed25519_public`
- `ssh_hostkey_ed25519_secret`
- `ssh_rng_seed`
- `ssh_signer_port`

## 起動経路メモ

### 通常

- `bin/start.sh --ssh`
- QEMU の `hostfwd` だけを追加する

### overlay を含めて作り直す経路

- `bin/restart.sh --ssh`
- `sodex-admin.conf` を生成して build し直してから起動する

### smoke

- `make -C src test-qemu-ssh`
- `src/test/run_qemu_ssh_smoke.py` が `OpenSSH` client を使って login / wrong password / reconnect を確認する想定

## signer 分離の余地

- `ssh_signer_port` が設定されると、署名と curve25519 計算を host 側 signer に逃がせる構造がある
- 既定経路では `ssh_signer_port=0` なので、通常は guest 側に注入した seed/material を使う
- 補助実装は `tests/ssh_signer.c`

## 関連ファイル

- `src/net/ssh_server.c`
- `src/net/admin_server.c`
- `src/lib/ssh_crypto.c`
- `src/include/ssh_server.h`
- `src/include/admin_server.h`
- `src/test/run_qemu_ssh_smoke.py`
- `src/test/write_server_runtime_overlay.py`
- `tests/test_ssh_crypto.c`
- `tests/ssh_signer.c`
- `specs/server-runtime/README.md`
- `specs/server-runtime/TASKS.md`
- `specs/server-runtime/plans/08-ssh-server.md`

## 未完了事項

- host key fingerprint と `known_hosts` を固定した手順
- 追加オプションなしの `ssh -p ... root@127.0.0.1` の確認
- auth retry 制限の SSH login path への統合
- packet / auth policy / channel state machine の host test 拡充
- 複数 channel や scope 外 request の reject をさらに明示的に固定すること

## 現時点の実務上の認識

- server runtime の管理用途としては、まず HTTP / admin protocol が主
- SSH は「最小 remote shell が通る」段階まで来ている
- ただし運用面では fingerprint 固定と retry 制限がまだ弱い
- そのため、現時点では完成済みの汎用 `sshd` と見なさず、最小機能の検証実装として扱うのが妥当
