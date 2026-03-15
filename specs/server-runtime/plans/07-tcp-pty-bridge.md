# Plan 07: 最小 TCP-PTY Bridge

## 概要

`SSH` をいきなり実装する前に、host から guest `sodex` の shell へ生の TCP でつなぐ最小の bridge を入れる。
目的は `telnet` 互換を作ることではなく、server 側の `PTY` / shell / session relay を先に検証すること。

## 目標

- host の raw TCP client から guest `eshell` へ接続できる
- `allowlist` / token / audit / timeout を維持したまま shell session を張れる
- `PTY` の生成、relay、切断、再接続の整合を固定できる

## スコープ

### 初期

- 専用の debug shell port
- 接続直後の短い認証 preface
- 認証後は raw byte stream のまま `PTY` と相互 relay
- 単一 active session
- `/usr/bin/eshell` に限定

### 後回し

- `telnet` option negotiation
- 複数同時 session
- file transfer
- 任意コマンド exec
- 公開向け remote shell

## 設計判断

- `telnet server` は作らない
- client は `nc` / `socat` のような raw TCP client を前提にする
- listener は default off にし、明示設定がある時だけ有効にする
- 既存の `allowlist` / rate limit / audit を再利用する
- 暗号化は入れず、`PTY` と session relay の検証を優先する

## プロトコル案

### 接続シーケンス

1. client が debug shell port に接続する
2. 最初の 1 行で token を送る
3. server が `PTY` を開いて `eshell` を起動する
4. 以後は raw stream を socket と `PTY` の間で中継する

### 初期例

- client: `TOKEN control-secret`
- server: `OK shell`

以降は line protocol ではなく byte relay に切り替える。

## 実装ステップ

1. debug shell listener と config key を定義する
2. 認証 preface と session upgrade を実装する
3. `openpty()` / `execve_pty()` で shell を起動する
4. socket <-> `PTY` の双方向 relay を実装する
5. EOF / close / timeout / reconnect を整理する
6. 単一 session 制限と audit event を入れる
7. winsize を固定値で始めるか、最小の resize 制御を入れるか決める
8. host test と QEMU smoke を追加する

## テスト

- host 側から raw TCP client で接続
- token なし / token 不一致の拒否
- shell prompt の表示
- command 実行と echo
- 切断後の再接続
- timeout / busy の確認

## 変更対象

- `src/net/debug_shell_server.c`
- `src/include/debug_shell_server.h`
- `src/kernel.c`
- `src/tty/tty.c`
- `src/execve.c`
- `src/test/run_qemu_debug_shell_smoke.py`
- `tests/test_debug_shell_parser.c` または同等の host test

## 完了条件

- [x] host から guest shell へ raw TCP で接続できる
- [x] token / allowlist なしでは shell を開けない
- [x] 切断後に session を回収し、再接続できる
- [x] `SSH` 実装前に server 側 `PTY` relay の基盤として使える
