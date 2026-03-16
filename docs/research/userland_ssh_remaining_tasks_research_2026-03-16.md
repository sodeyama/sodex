# userland ssh 残タスク調査メモ

**調査日**: 2026-03-16
**対象**: `USS-03`, `USS-09`, `USS-12`
**目的**: 外部一次資料を基に、shared core 分離、cutover cleanup、auth hardening の残作業を再定義する

---

## 1. 結論

- `USS-03` は「1 つの巨大 `ssh_server.c` をそのまま userland 化する」方向ではなく、`transport/codec`、`auth`、`channel/session` の 3 層に分け、socket / `PTY` / audit / config / spawn は adapter に残す方針で進める
- `USS-09` は機能追加ではなく cleanup の phase として扱い、`ssh_server_tick()` 前提、userland 専用 wrapper、bring-up 用 helper を shared core から外す
- `USS-12` は OpenSSH や RFC より厳しめの最小 profile を維持してよいが、`pre-auth grace`、`post-auth no-channel`、`session idle` を分け、auth failure delay と username/service pinning を追加するのが妥当

今回の判断は、一次資料の一般論をそのままコピーするのではなく、Sodex の「単一接続・単一 session・単一 user」前提へ落とした設計上の推論を含む。

---

## 2. 一次資料から確認したこと

### 2.1 OpenSSH は責務ごとに file を分けている

OpenSSH portable の repository root では、`auth2.c`、`channels.c`、`audit.c`、`README.privsep` などが分かれており、認証、channel、audit、権限分離を別責務として扱っている。
また OpenBSD `sshd_config(5)` には `SshdAuthPath` と `SshdSessionPath` があり、`sshd-auth` と `sshd-session` を別 binary として差し替えられる。

ここからの判断:

- 認証と session 処理を分ける設計は一般的
- Sodex でも `ssh_server.c` の monolith をそのまま維持するより、shared core + adapter へ分ける方が自然
- `USS-03` では「kernel と userland で同じ file を ifdef で耐える」のではなく、責務で file を切る方が後続の `USS-09` cleanup に効く

### 2.2 認証 timeout と retry 制限は protocol 上も推奨されている

RFC 4252 Section 4 は、server が authentication timeout を持つこと、1 session あたりの failed authentication attempts を制限すること、閾値超過時に disconnect することを推奨している。
同 RFC Section 5 は、`user name` と `service name` を各 authentication request で再確認し、変化した場合は state を flush できなければ disconnect すべきとしている。

ここからの判断:

- Sodex の `auth_timeout` と retry 上限は protocol 的にも妥当
- ただし現状の `3` 回切断だけでは足りず、username/service の pinning まで入れて `USS-12` を閉じるべき

### 2.3 OpenSSH の既定値は緩めだが、Sodex にそのまま合わせる必要はない

OpenBSD `sshd_config(5)` では `MaxAuthTries` の既定値は `6`、`LoginGraceTime` は `120 seconds`、`MaxSessions` は既定 `10` で `1` にすると multiplexing を実質無効化できる。
同 man page には `UnusedConnectionTimeout` と `ChannelTimeout` もあり、「認証後だが channel 未確立」と「channel 確立後の idle」を別 timeout として扱っている。

ここからの判断:

- Sodex は `MaxSessions=1` 相当の単一 session 制限を維持してよい
- ただし timeout は `auth_done` の真偽だけでは粗いので、`no-channel` と `idle` を分けるべき
- `3` 回切断は OpenSSH より厳しいが、single-user appliance の profile としては許容できる

### 2.4 Channel close 順序は RFC 4254 の契約で固定するべき

RFC 4254 Section 5.3 は、送信終了時に `SSH_MSG_CHANNEL_EOF` を送り、完全 close では `SSH_MSG_CHANNEL_CLOSE` を送り、相手から close を受けた側も close を返すことを定めている。
Section 6.10 は `exit-status` を返すなら `SSH_MSG_CHANNEL_CLOSE` より前に送ることを推奨している。

ここからの判断:

- `shell_exit`、client disconnect、EOF close の扱いは `USS-09` の cleanup 対象であり、単なる実装都合ではなく protocol 契約として固定するべき
- reconnect 回帰を防ぐには、channel close ordering と socket close / stale child cleanup を同じ plan に入れる必要がある

### 2.5 OpenSSH 実装は auth failure に最小 delay を入れている

OpenSSH `auth2.c` には `MIN_FAIL_DELAY_SECONDS` / `MAX_FAIL_DELAY_SECONDS` と `ensure_minimum_time_since()` があり、失敗応答を即時に返さない工夫が入っている。

ここからの判断:

- `USS-12` の backoff は protocol 必須ではないが、実装 hardening として妥当
- ただし Sodex は現状 single connection なので、まずは「接続単位の固定 delay」を入れ、`PerSourcePenalties` 相当の source 単位 throttling は将来課題へ送るのが現実的

---

## 3. Sodex への適用判断

### 3.1 `USS-03`: shared core の分離単位

`src/net/ssh_server.c` から先に切るべき責務は次の 4 つ。

1. `transport/codec`
   - `ssh_queue_payload()`
   - `ssh_try_decode_plain_packet()`
   - `ssh_try_decode_encrypted_packet()`
   - writer / reader helper、`name-list` 判定
2. `auth`
   - `ssh_handle_service_request()`
   - `ssh_handle_userauth_request()`
   - `ssh_handle_auth_failure()`
   - timeout / retry / username-service pinning policy
3. `channel/session`
   - `ssh_handle_channel_open()`
   - `ssh_handle_channel_request()`
   - `ssh_handle_channel_data()`
   - `ssh_queue_exit_status()` / `ssh_queue_channel_eof()` / `ssh_queue_channel_close()`
4. adapter
   - listener create / accept / send / recv
   - `PTY` alloc / spawn / winsize / foreground signal
   - audit / config / RNG / hostkey / clock
   - userland `main()` loop と `poll` 待機

この分離なら、kernel build と userland build の差は adapter 側へ押し込める。

### 3.2 `USS-09`: cleanup の主対象

残っている cleanup は主に 4 系統。

1. `ssh_server_tick()` 依存
   - shared core が `kernel_tick` と global singleton を前提にしている
   - `step(now_ticks, events)` 相当の driver へ寄せ、kernel compat shim は最後に薄く残す
2. userland 専用 wrapper の居残り
   - `USERLAND_SSHD_BUILD` 下の `server_runtime_*` / `server_audit_*`
   - socket table / poll wrapper / `main()` loop
3. bring-up 用 helper / audit の整理
   - bootstrap でしか使わない helper を adapter へ寄せる
   - 既存 smoke が見ている audit line 名だけ残す
4. close/release の責務整理
   - `shell_exit`、client disconnect、`recv == 0`、`POLLHUP`
   - stale child / stale `uip_conn` cleanup

### 3.3 `USS-12`: hardening の主対象

残っている hardening は次の順で進めるべき。

1. policy module を inline header から stateful API へ寄せる
2. timeout を 3 種類へ分離する
   - `auth_grace`
   - `post_auth_no_channel`
   - `session_idle`
3. auth request の username/service を初回で pin し、変化したら disconnect する
4. auth failure ごとに固定 delay を入れる
5. しきい値は host test と QEMU smoke を通して再調整する

`PerSourcePenalties` や複数 unauthenticated connection の drop 制御は、listener が複数接続 state を持つ段階まで defer する。

---

## 4. 詳細な対応計画

### 4.1 `USS-03`

1. `ssh_packet_core`
   - packet encode / decode、payload queue、`name-list` helper を切り出す
   - `tests/test_ssh_packet.c` を追加し、plain/encrypted decode、window 上限、invalid packet を host test 化する
2. `ssh_auth_core`
   - service accept、password auth、failure response、policy 判定を切り出す
   - `tests/test_ssh_auth.c` を追加し、wrong password、service change、retry limit、partial-success 非対応を固定する
3. `ssh_channel_core`
   - `session` open、`pty-req`、`window-change`、`shell` request、`exit-status` / `EOF` / `CLOSE` ordering を切り出す
   - `tests/test_ssh_channel.c` を追加し、単一 channel 制約、close ordering、peer window 減算を固定する
4. adapter 接続
   - kernel 版と userland 版は「socket / tty / spawn / audit / config を埋めるだけ」の構造に寄せる
   - `src/net/ssh_server.c` は最終的に adapter glue と compat wrapper のみへ縮める

### 4.2 `USS-09`

1. `USERLAND_SSHD_BUILD` 専用 wrapper を別 file へ移す
2. shared core から `kernel_tick` / global singleton 依存を外す
3. `ssh_server_tick()` は compat 用の薄い入口へ縮める
4. `recv == 0`、`POLLHUP`、shell exit、peer close を同じ close state machine に寄せる
5. audit line を棚卸しし、smoke で使うもの以外の bring-up 用 line を削る

### 4.3 `USS-12`

1. `ssh_runtime_policy` を `struct ssh_runtime_policy` ベースへ拡張する
2. timeout 判定を `auth_done` 二択から 3 段階へ分ける
3. username/service pinning を auth state に追加する
4. auth failure delay を接続単位で追加する
5. host test と `test-qemu-ssh` で wrong password / idle / reconnect / client disconnect を再固定する

---

## 5. 今回の plan 更新に反映すること

- `Plan 03` に shared core の分離単位、抽出順、test 追加順を明記する
- `Plan 06` に `USS-09` cleanup と `USS-12` hardening を分けて書く
- `TASKS.md` の `USS-03` / `USS-09` / `USS-12` の完了条件を、今回の分解に合わせて具体化する

---

## 6. 参考資料

一次資料のみ。

- OpenBSD `sshd_config(5)`: https://man.openbsd.org/sshd_config
- RFC 4252, "The Secure Shell (SSH) Authentication Protocol": https://www.rfc-editor.org/rfc/rfc4252
- RFC 4254, "The Secure Shell (SSH) Connection Protocol": https://www.rfc-editor.org/rfc/rfc4254.html
- OpenSSH portable repository: https://github.com/openssh/openssh-portable
- OpenSSH `README.privsep`: https://raw.githubusercontent.com/openssh/openssh-portable/master/README.privsep
- OpenSSH `auth2.c`: https://raw.githubusercontent.com/openssh/openssh-portable/master/auth2.c

---

## 7. 補足

OpenSSH の既定値や helper 分割は、そのまま Sodex の仕様ではない。
今回の plan は「OpenSSH / RFC が分けている契約」と「Sodex の最小 single-session 実装」を突き合わせた結果として定めたものである。
