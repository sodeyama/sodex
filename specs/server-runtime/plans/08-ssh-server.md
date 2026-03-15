# Plan 08: SSH Server の段階導入

## 概要

Plan 07 の TCP-PTY bridge で server 側 `PTY` / session relay を固定した後に、
暗号化を含む最小の `SSH server` を段階的に入れる。
目的は最初から完全な `sshd` 互換を目指すことではなく、`OpenSSH` client から接続できる最小実装を作ること。

## 目標

- `ssh` client から guest `sodex` に接続できる
- host key を注入または永続化できる
- 最初の 1 transport suite と 1 auth 方式で login できる
- `session` channel + `pty-req` + `shell` を成立させる

## スコープ

### 初期

- `SSHv2` のみ
- 最小の algorithm suite ひとつ
- 最小の auth method ひとつ
- `session` channel
- `pty-req`, `shell`, `window-change`, `exit-status`

### 後回し

- `scp`, `sftp`
- port forwarding
- agent forwarding
- 複数 auth method
- 複数 algorithm suite
- 公開 internet 向け hardening 一式

## 設計判断

- 先に Plan 07 で `PTY` relay を固定し、暗号化と shell relay を分離して進める
- 既存の `allowlist` / audit / connection limit を外さない
- host key がなければ fail closed にする
- crypto は最初の 1 suite に絞り、相互接続性を優先する
- shell 以外の subsystem は初期スコープに入れない

## 実装ステップ

1. 乱数源と crypto 実装の選定を行う
2. host key の保存 / 注入形式を決める
3. version exchange と packet framing を実装する
4. `KEXINIT` / key exchange / `NEWKEYS` を実装する
5. transport cipher / integrity 保護を実装する
6. user authentication を 1 方式で実装する
7. `session` channel / `pty-req` / `shell` を実装する
8. `window-change` / close / reconnect を整理する
9. `OpenSSH` client との互換 smoke を追加する

## テスト

- `ssh` で接続して login できる
- host key の fingerprint が固定される
- 認証失敗を拒否できる
- shell prompt 表示と command 実行
- 切断、再接続、window resize
- 想定外 packet や handshake 失敗の拒否

## 変更対象

- `src/net/ssh_server.c`
- `src/include/ssh_server.h`
- `src/lib/crypto/*` または導入する最小 crypto 実装
- `src/kernel.c`
- `src/tty/tty.c`
- `src/execve.c`
- `src/test/run_qemu_ssh_smoke.py`
- `tests/test_ssh_packet.c` などの host test

## 完了条件

- [ ] `OpenSSH` client から接続できる
- [ ] host key / auth / session の最小経路が成立する
- [ ] shell 接続の切断と再接続が安定する
- [ ] 初期スコープ外の subsystem を開けずに運用できる
