# Plan 06: Cutover と Hardening

## 概要

userland `sshd` が動いたら、既定起動を切り替え、
最後に kernel listener を外す。
ここでは parity 確認と cleanup を中心に進める。

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

## テスト固定

- host test
  - packet / auth / channel
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
