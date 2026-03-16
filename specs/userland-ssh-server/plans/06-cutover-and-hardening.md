# Plan 06: Cutover と Hardening

## 概要

userland `sshd` が動いたら、既定起動を切り替え、
最後に kernel listener を外す。
ここでは parity 確認に加えて、`USS-09` cleanup と `USS-12`
hardening を分けて閉じる。

2026-03-16 の再調査では、RFC 4254 が `exit-status` / `EOF` / `CLOSE`
の順序を、OpenBSD `sshd_config(5)` が `UnusedConnectionTimeout` と
`ChannelTimeout` の分離を示していることを確認した。
この plan では、その 2 点を cutover 後の完了条件へ直接入れる。

## 実装結果

- `src/usr/command/sshd.c` が userland `sshd` の wait/main loop を持ち、
  `ssh_server.c` は bootstrap / pending / refresh hook を公開する形へ整理した
- `shell_exit` の `exit-status` -> `EOF` -> `CLOSE` は
  `ssh_channel_plan_shutdown()` で固定し、client disconnect / reconnect は
  `test-qemu-ssh` で再確認した
- auth / timeout は `auth_timeout`、`no_channel_timeout`、`idle_timeout` の
  3 段階へ分離し、username/service pinning、retry delay、3 回失敗で close、
  OpenSSH の `none` auth probe への `USERAUTH_FAILURE` を入れた

## 切り替え順

1. feature flag で kernel SSH / userland `sshd` を切り替えられるようにする
2. `server-headless` から userland `sshd` を既定にする
3. `server` でも既定にする
4. smoke と manual 手順を userland 前提に更新する
5. kernel listener と `ssh_server_tick()` を削る

## hardening 項目

- auth retry 制限を userland path にも維持する
- ready marker と audit line の互換をなるべく維持する
- config 不備時 fail-safe を維持する
- known_hosts / fingerprint の手順を整理する

## `USS-09`: cleanup

### 目標

- 既定経路の cutover 完了後に残った kernel listener / global tick 依存を整理する
- bring-up 用 helper を shared core から外す
- reconnect / shell exit / client disconnect の close ordering を protocol 契約として固定する

### やること

1. `USERLAND_SSHD_BUILD` 専用 wrapper を別 file へ移す
   - `server_runtime_*`
   - `server_audit_*`
   - socket table / poll wrapper
   - userland `main()` loop
2. shared core から `kernel_tick` と global singleton 依存を減らす
3. `ssh_server_tick()` を compat 用の薄い入口へ縮める
4. `recv == 0`、`POLLHUP`、shell exit、peer close を同じ close state machine へ寄せる
5. smoke が見ていない bring-up 用 audit/helper を削る

### 受け入れ条件

- `ssh_server.c` の userland 専用 wrapper が shared core から外れている
- `shell_exit` 時に `exit-status` -> `EOF` -> `CLOSE` の順で queue される
- client disconnect / reconnect の回帰が再発しない
- `server` / `server-headless` の既定経路は変えずに green を維持する

現状では wait/main loop を `src/usr/command/sshd.c` へ出し、
close ordering と reconnect 回帰も smoke で固定できたため、
`USS-09` は完了扱いとする。

## `USS-12`: auth / timeout hardening

### 目標

- 単一接続制限を維持したまま、auth / idle policy を protocol 的に無理のない形へ寄せる
- OpenSSH より厳しめの profile は維持するが、理由と境界を明確化する

### やること

1. `ssh_runtime_policy` を stateful API へ広げる
2. timeout を 3 段階へ分離する
   - `auth_grace`
   - `post_auth_no_channel`
   - `session_idle`
3. auth request の username / service を初回で pin し、変化時は disconnect する
4. auth failure ごとに固定 delay を入れる
5. retry limit と timeout 閾値を host test / QEMU smoke を通して再調整する

### スコープ外

- `MaxStartups` 相当の複数 unauthenticated connection 制御
- `PerSourcePenalties` 相当の source 単位 throttling
- multi-session / multiplexing 対応

### 受け入れ条件

- `auth_timeout`、`no_channel_timeout`、`idle_timeout` を区別して close reason を出せる
- wrong password の連続試行で delay と retry limit が効く
- username / service change を disconnect として扱える
- `test-qemu-ssh` の login / wrong password / reconnect / client disconnect が通る

上記は host test と `test-qemu-ssh` で満たしたため、`USS-12` は完了扱いとする。

## テスト固定

- host test
  - packet / auth / channel
  - timeout / retry / username-service pinning
- QEMU smoke
  - login
  - wrong password
  - reconnect
  - client disconnect
  - shell exit

## 完了条件

- `server` / `server-headless` の既定 SSH が userland `sshd` になる
- 既存の主要 smoke が userland `sshd` 前提で green になる
- kernel 側 SSH 常駐処理が消える
- README と spec が新しい実装位置を前提に更新される
- `USS-09` cleanup と `USS-12` hardening の受け入れ条件が両方満たされる

## 参考

- OpenBSD `sshd_config(5)`: https://man.openbsd.org/sshd_config
- RFC 4252: https://www.rfc-editor.org/rfc/rfc4252
- RFC 4254: https://www.rfc-editor.org/rfc/rfc4254.html
- OpenSSH `auth2.c`: https://raw.githubusercontent.com/openssh/openssh-portable/master/auth2.c
