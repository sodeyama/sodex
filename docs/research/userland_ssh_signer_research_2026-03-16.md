# userland sshd signer 調査メモ

**調査日**: 2026-03-16  
**対象**: `src/net/ssh_server.c` の userland `sshd` における signer 依存  
**目的**: `bin/restart.sh server-headless --ssh` 単体で接続できない原因と、妥当な修正方針を一次資料ベースで整理する

---

## 1. 結論

今の userland `sshd` が `ssh_signer_port=0` でも signer を必須扱いしているのは、SSH protocol の要件とも OpenSSH の一般実装とも整合していない。

最小の修正方針は次の通り。

- `ssh_signer_port=0` のときは guest 内で `curve25519` と `Ed25519` 署名を完結させる
- `ssh_signer_port>0` のときだけ signer を使う
- さらに整理するなら、signer に逃がすのは host private key を使う署名だけに寄せ、ephemeral な `curve25519` 計算は guest 側で行う

この方針なら、既存の `bin/restart.sh server-headless --ssh` を壊さず、spec に書かれている「既定では signer なし」を回復できる。

---

## 2. 一次資料から分かること

### 2.1 RFC 5656 / RFC 8731 の要件

SSH の ECDH / Curve25519 系 KEX では、server 側は次を行う。

- client の ephemeral public key を受け取る
- server 自身で ephemeral key pair を生成する
- shared secret を計算する
- exchange hash `H` を作る
- host key で `H` に署名する

RFC 8731 は `curve25519-sha256` の手順が RFC 5656 の ECDH 手順と同じだと明記している。つまり、`curve25519` の共有鍵計算そのものに external signer は不要で、protocol 上も要求されていない。

RFC 8709 では `ssh-ed25519` は「署名専用」の host key algorithm と定義されている。ここでも役割は host key 署名であり、KEX の共有鍵計算とは別である。

### 2.2 OpenSSH の一般実装

OpenSSH の `kexgen.c` では、`curve25519-sha256` の server 側処理で `kex_c25519_enc()` を呼んで server public key と shared secret をローカル計算し、その後に `kex->sign(...)` で exchange hash を署名している。

つまり OpenSSH は:

- KEX の ephemeral `curve25519` はローカル計算
- host key 署名だけを callback で差し替え可能

という構造である。

加えて OpenBSD `sshd_config` の `HostKeyAgent` は「private host keys への操作を `ssh-agent` に委譲できる」と説明している。これは signer/agent を使うとしても host private key 操作の委譲が主眼であり、KEX 全体を helper 前提にしていないことを示している。

---

## 3. Sodex 現状とのズレ

`src/net/ssh_server.c` の `USERLAND_SSHD_BUILD` 経路では、`ssh_handle_kex_init()` が signer の有無に関係なく:

- `ssh_request_host_curve25519(...)`
- `ssh_request_host_signature(...)`

を呼ぶ。

しかし両関数の userland 側実装は、`admin_runtime_ssh_signer_port() <= 0` なら即 `-1` を返す。このため `ssh_signer_port=0` では KEX 開始直後に失敗する。

実際に手元では次を再現した。

- `bin/restart.sh server-headless --ssh`
- host から `ssh -p 10022 root@127.0.0.1`
- serial audit は `ssh_kex_client_public_ok` の直後に `reason=protocol_error`

一方で host 側で `tests/ssh_signer` を立て、`SODEX_SSH_SIGNER_PORT=10026` を与えると password prompt までは進む。したがって、壊れているのは起動 script ではなく userland `sshd` の signer 依存である。

この挙動は `specs/server-runtime/SSH-WORKING.md` の以下とも矛盾する。

- `ssh_signer_port` が設定されると host 側 signer に逃がせる
- 既定経路では `ssh_signer_port=0`
- 通常は guest 側に注入した seed/material を使う

---

## 4. 推奨修正案

### 案 A: kernel build と同じ分岐にそろえる

最小修正として、`USERLAND_SSHD_BUILD` でも kernel build 側と同じ条件分岐にする。

- signer port あり: signer を使う
- signer port なし: local `curve25519` + local `ed25519 sign`

期待効果:

- `bin/restart.sh server-headless --ssh` 単体で再び動く
- 既存 spec と整合する
- 現在の helper 実装をそのまま温存できる

### 案 B: signer は host key 署名だけに限定する

より筋の良い設計は、helper の責務を host key 署名だけに絞ること。

- `curve25519` の ephemeral secret は session ごとの一時値であり、host key 保護の論点とは別
- RFC / OpenSSH の一般実装にも沿う
- UDP 往復が 1 回減る
- 切り分けしやすい

必要なら将来の高速化や guest の計算削減のために `curve25519` helper を残してもよいが、少なくとも既定経路では必須にしないほうがよい。

### 案 C: signer 必須モードを明示的に分ける

signer 前提の検証モードを残したいなら、既定の `--ssh` とは別に明示フラグを作るほうが安全。

- 例: `--ssh-signer-port=10026`
- 既定の `--ssh` は signer なしで完結
- signer ありは opt-in

---

## 5. 実装時の確認ポイント

- `ssh_signer_port=0` で password prompt まで進むこと
- `ssh_signer_port>0` で signer 経由でも従来どおり通ること
- `wrong password` と reconnect が落ちないこと
- signer 未起動かつ `ssh_signer_port>0` のとき、今の generic な `protocol_error` ではなく、KEX failure と分かる audit を出すこと

---

## 6. 参考資料

一次資料のみ。

- RFC 5656, "SSH ECC Algorithm Integration": https://www.rfc-editor.org/rfc/rfc5656.txt
- RFC 8731, "Secure Shell (SSH) Key Exchange Method Using Curve25519 and Curve448": https://www.rfc-editor.org/rfc/rfc8731.txt
- RFC 8709, "Ed25519 and Ed448 Public Key Algorithms for the Secure Shell (SSH) Protocol": https://www.rfc-editor.org/rfc/rfc8709.txt
- OpenSSH Portable `kexgen.c`: https://github.com/openssh/openssh-portable/blob/master/kexgen.c
- OpenBSD `sshd_config` man page (`HostKeyAgent`): https://man.openbsd.org/sshd_config

---

## 7. この repo 内の関連箇所

- `src/net/ssh_server.c`
- `tests/ssh_signer.c`
- `specs/server-runtime/SSH-WORKING.md`
