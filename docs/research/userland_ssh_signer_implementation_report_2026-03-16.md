# userland sshd signer 実装課題 詳細調査

**調査日**: 2026-03-16  
**対象**: userland `sshd` が `bin/restart.sh server-headless --ssh` 単体で接続できない問題  
**主対象ファイル**:

- `src/net/ssh_server.c`
- `src/syscall.c`
- `src/net/admin_server.c`
- `bin/restart.sh`
- `src/test/run_qemu_ssh_smoke.py`

**外部一次資料**:

- RFC 4253
- RFC 5656
- RFC 8731
- RFC 8709
- OpenSSH Portable `kexgen.c`
- OpenBSD `sshd_config(5)`

---

## 1. エグゼクティブサマリー

現状の本質的な問題は、userland `sshd` が **既に repo 内にある正しい抽象化を使わず**、`USERLAND_SSHD_BUILD` の KEX 経路だけ独自に external signer 前提へ分岐してしまっている点にある。

特に重要なのは次の 4 点。

1. `src/net/ssh_server.c` の userland KEX 経路は `ssh_signer_port=0` でも external signer を必須扱いしている
2. しかしカーネルには `SYS_CALL_SSH_SIGNER_ROUNDTRIP` が既にあり、`port<=0` なら local crypto、`port>0` なら host signer へ送る実装がある
3. つまり userland `sshd` は **既存の syscall helper を使えばそのまま解決できる構造**になっている
4. さらに protocol 的には、`curve25519` の共有鍵計算を signer に逃がす必要はなく、OpenSSH も通常そうしていない

最短修正は:

- userland 側の `ssh_request_host_signature()` / `ssh_request_host_curve25519()` を `ssh_signer_roundtrip()` ベースに統一する

より筋の良い修正は:

- `curve25519` は常に guest 側で計算
- signer は host key 署名の委譲だけに使う

---

## 2. 再現した現象

`bin/restart.sh server-headless --ssh` で起動し、host から:

```sh
ssh -tt -F /dev/null \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -p 10022 root@127.0.0.1
```

を実行すると、接続は即 close される。

serial audit では次の流れを確認した。

- `listener_ready kind=ssh port=10022`
- `accept_ssh peer=10.0.2.2`
- `ssh_banner_rx_ok`
- `ssh_kex_client_public_ok`
- `ssh_close peer=10.0.2.2 reason=protocol_error`

一方で host 側で `tests/ssh_signer` を起動し、`SODEX_SSH_SIGNER_PORT=10026` を付けて起動すると password prompt までは進む。  
したがって障害箇所は起動 script ではなく、userland `sshd` の KEX 実装である。

---

## 3. 実装上の主要課題

## 3.1 userland KEX が `ssh_signer_port=0` でも signer を必須扱いしている

`src/net/ssh_server.c` の `ssh_handle_kex_init()` では、`USERLAND_SSHD_BUILD` の場合に無条件で:

- `ssh_request_host_curve25519(...)`
- `ssh_request_host_signature(...)`

を呼んでいる。

しかし userland 側の両関数は、`admin_runtime_ssh_signer_port() <= 0` なら即 `-1` を返す。  
これにより、既定の `ssh_signer_port=0` で KEX が必ず失敗する。

この実装は、同じファイル内の non-userland 分岐と不整合である。non-userland 側は:

- signer port あり: remote signer を使う
- signer port なし: local `curve25519` / local `ed25519 sign`

という自然な分岐になっている。

### 評価

- **重大度**: P0
- **性質**: 直接の回帰原因
- **修正難度**: 低い

---

## 3.2 既にある `SYS_CALL_SSH_SIGNER_ROUNDTRIP` を userland `sshd` が使っていない

`src/syscall.c` には `sys_ssh_signer_roundtrip()` が実装済みで、挙動は次の通り。

- `port <= 0`: `sys_ssh_signer_local_roundtrip()` を使い、カーネル内の local crypto で処理
- `port > 0`: UDP で host signer に問い合わせ

userland からは `src/usr/include/sys/socket.h` に:

```c
int ssh_signer_roundtrip(int port, const void *request, int request_len,
                         void *response, int response_len);
```

が公開されている。

つまり、この syscall helper を使えば userland `sshd` は:

- local fallback
- remote signer
- port の取り扱い
- retry の共通化

を 1 つの呼び出しで済ませられる。

にもかかわらず、`src/net/ssh_server.c` の userland 実装は独自に:

- UDP socket を作る
- `network_fill_gateway_addr()` を呼ぶ
- `sendto/recvfrom` を回す

という別実装を持っている。これが drift の直接原因になっている。

### 評価

- **重大度**: P0
- **性質**: アーキテクチャ上の分岐重複
- **修正難度**: 低い

### 推奨

最短では userland 側の signer request 実装を `ssh_signer_roundtrip()` 呼び出しに置き換えるべきである。

---

## 3.3 signer の責務が曖昧で、host key 保護モデルにもなっていない

現在の仕様メモでは `ssh_signer_port` は「署名と curve25519 計算を host 側 signer に逃がせる構造」とされている。  
しかし現実の実装では、`admin_runtime_copy_ssh_config()` が userland へ以下をそのままコピーしている。

- `ssh_hostkey_ed25519_seed`
- `ssh_hostkey_ed25519_public`
- `ssh_hostkey_ed25519_secret`
- `ssh_rng_seed`

つまり userland `sshd` は、すでに hostkey secret を受け取っている。  
この状態では「private key 操作を外部 signer に委譲することで secret を隠す」という設計上の意味は薄い。

### 実装上の含意

- `curve25519` 共有鍵計算を signer に出す理由は弱い
- `ed25519` 署名を signer に出す理由も、少なくとも「秘密鍵を userland に渡さないため」では説明できない
- 今の signer は「セキュリティ境界」ではなく、単なる helper に近い

### 評価

- **重大度**: P1
- **性質**: 設計一貫性の欠如
- **修正難度**: 中

### 推奨

今後の方針はどちらかに寄せるべき。

- 方針 A: signer は単なる補助計算器と割り切る
- 方針 B: hostkey secret を userland へ渡さず、署名だけ kernel/agent に閉じる

現時点では方針 A のほうが修正コストは低い。

---

## 3.4 `curve25519` shared secret の all-zero check が無い

RFC 8731 は `curve25519-sha256` について、X25519 の計算結果が all-zero なら abort することを要求している。  
OpenSSH も `kexc25519.c` で shared secret の all-zero check を実装している。

しかし Sodex の `src/lib/ssh_crypto.c` にある `ssh_crypto_curve25519_shared()` は、単に `crypto_scalarmult()` を呼ぶだけで、all-zero check をしていない。

このため、もし不正な public key を受け取った場合でも:

- shared secret が all-zero
- それをそのまま KDF に通す

という経路に入る可能性がある。

### 評価

- **重大度**: P1
- **性質**: protocol / security gap
- **修正難度**: 低い

### 推奨

`ssh_crypto_curve25519_shared_checked()` のような helper を追加し、32 byte 全ゼロなら失敗させる。

---

## 3.5 KEX failure が `protocol_error` に潰され、`SSH_MSG_DISCONNECT` も送っていない

今の `ssh_poll_connection()` は、`ssh_handle_payload()` が負を返したときに:

- `ssh_queue_close(conn, "protocol_error")`

としている。

しかし `ssh_queue_close()` は audit を残すだけで、transport-level の `SSH_MSG_DISCONNECT` を積まない。  
結果として client からは「TCP が閉じた」としか見えず、KEX failure / MAC error / auth timeout の区別がつかない。

RFC 4253 には disconnect reason code が定義されており、少なくとも KEX failure には `SSH_DISCONNECT_KEY_EXCHANGE_FAILED` を対応させるのが自然である。

### 評価

- **重大度**: P1
- **性質**: protocol observability / debuggability 不足
- **修正難度**: 中

### 推奨

最低限:

- audit reason を `protocol_error` ではなく `kex_failed_*` に分ける

余力があれば:

- `SSH_MSG_DISCONNECT` を送る helper を追加する
- `SSH_DISCONNECT_KEY_EXCHANGE_FAILED`
- `SSH_DISCONNECT_PROTOCOL_ERROR`
- `SSH_DISCONNECT_MAC_ERROR`

を使い分ける

---

## 3.6 script / smoke / spec の期待値が揃っていない

`src/test/run_qemu_ssh_smoke.py` の既定値は `SSH_SIGNER_PORT=0` であり、spec でも「既定では signer なし」としている。  
つまりテスト・設計上の期待は最初から signer-less である。

一方で `bin/restart.sh` は:

- `ssh_signer_port` を CLI で受け取らない
- `SODEX_SSH_SIGNER_PORT` を export しない
- README の手動手順も signer を要求していない

したがって、仮に今の実装が signer 必須だったとしても、その必須条件を start/restart 手順が表現できていない。

### 評価

- **重大度**: P2
- **性質**: 運用・ドキュメント不整合
- **修正難度**: 低い

### 推奨

- 本筋は signer 必須をやめること
- もし signer mode を残すなら `--ssh-signer-port=` を `start.sh` / `restart.sh` に追加する

---

## 4. 一次資料との照合

## 4.1 RFC 5656 / RFC 8731

両 RFC を合わせると、server 側 KEX の役割は次の順序になる。

1. client ephemeral public key を受信
2. server ephemeral key pair を生成
3. shared secret を計算
4. exchange hash `H` を作る
5. host key で `H` を署名

ここで external signer が関与し得るのは本質的には 5 であり、2-3 は通常 server 自身が行う。

## 4.2 OpenSSH

OpenSSH `kexgen.c` では:

- `kex_c25519_enc()` で `curve25519` の server public key / shared secret をローカル計算
- `kex->sign(...)` で host key 署名を抽象化

としている。

さらに `HostKeyAgent` の man page は、private host key の操作を agent に委譲できると説明している。  
これは「agent に出すとしても host private key の署名操作が中心」という設計を裏付ける。

---

## 5. 実装戦略の比較

## 5.1 戦略 A: `ssh_signer_roundtrip()` へ統一

### 内容

- userland `ssh_request_host_signature()` を syscall helper 呼び出しに置換
- userland `ssh_request_host_curve25519()` も syscall helper 呼び出しに置換
- helper 側の `port<=0` fallback をそのまま使う

### 利点

- 最短で直る
- local/remote の分岐重複が消える
- 既存の kernel helper を再利用できる

### 欠点

- `curve25519` helper 依存という設計自体は残る

### 推奨度

- **短期の第一選択**

## 5.2 戦略 B: `curve25519` は local 固定、署名だけ signer 抽象化

### 内容

- `ssh_crypto_curve25519_public_key()` / `ssh_crypto_curve25519_shared()` を常用
- signer は `ssh_request_host_signature()` のみで使う
- signer port 0 なら local sign、>0 なら remote sign

### 利点

- RFC / OpenSSH に近い
- helper 往復が 1 回減る
- KEX の追跡が簡単になる

### 欠点

- 戦略 A より変更差分がやや広い

### 推奨度

- **中期の本命**

## 5.3 戦略 C: signer を完全に削除

### 内容

- signer port 関連を削除
- local crypto のみ

### 利点

- 最も単純

### 欠点

- host 側 helper を使う検証経路を失う
- spec に書いた拡張余地が消える

### 推奨度

- 現時点では低い

---

## 6. 優先度付き修正タスク

## P0

- userland signer request 実装を `ssh_signer_roundtrip()` に統一
- `ssh_signer_port=0` で KEX が通ることを smoke で確認

## P1

- `curve25519` shared secret の all-zero check を追加
- KEX failure を `protocol_error` から分離
- 可能なら `SSH_MSG_DISCONNECT` を導入

## P2

- signer の責務を host key 署名中心に整理
- `admin_runtime_copy_ssh_config()` で secret を userland に渡す設計を見直す
- `start.sh` / `restart.sh` に signer mode の CLI を追加するか、不要なら削除

---

## 7. 受け入れ条件

最低限の完了条件は次の通り。

- `bin/restart.sh server-headless --ssh` だけで接続できる
- `ssh -p 10022 root@127.0.0.1` が password prompt まで進む
- `src/test/run_qemu_ssh_smoke.py` の既定値 `SSH_SIGNER_PORT=0` で green
- signer を明示したときも login / wrong password / reconnect が通る
- KEX failure 時の audit が `protocol_error` 1 本ではなく、失敗種別を識別できる

---

## 8. 参考資料

- RFC 4253: https://www.rfc-editor.org/rfc/rfc4253.txt
- RFC 5656: https://www.rfc-editor.org/rfc/rfc5656.txt
- RFC 8731: https://www.rfc-editor.org/rfc/rfc8731.txt
- RFC 8709: https://www.rfc-editor.org/rfc/rfc8709.txt
- OpenSSH Portable `kexgen.c`: https://github.com/openssh/openssh-portable/blob/master/kexgen.c
- OpenSSH Portable `kexc25519.c`: https://github.com/openssh/openssh-portable/blob/master/kexc25519.c
- OpenBSD `sshd_config(5)`: https://man.openbsd.org/sshd_config
