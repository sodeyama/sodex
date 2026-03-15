# Plan 03: 軽量管理プロトコル

## 概要

`SSH` をいきなり実装する代わりに、まずは小さな管理用プロトコルを入れる。
目的は「remote shell」ではなく、「限定された操作を安全に叩けること」。

## 目標

- host から guest `sodex` の状態を見られる
- Agent loop の開始 / 停止 / 状態確認ができる
- 任意シェル実行ではなく、命令を列挙型で制御できる

## プロトコル案

### 最初の形

- 1行1コマンドの text protocol
- 例:
  - `PING`
  - `STATUS`
  - `AGENT START`
  - `AGENT STOP`
  - `LOG TAIL 50`

### 返答

- `OK ...`
- `ERR ...`
- 長い出力は行単位または length-prefix

## 設計判断

- shell を露出しない
- `exec` 的な自由コマンドは初期スコープ外
- 操作対象は `named action` に限定する
- 文字列パースは固定上限長を持つ

## 実装ステップ

1. 管理 server の受信ループを作る
2. 小さな command parser を作る
3. `status`, `health`, `agent start/stop` を先に入れる
4. ログや統計の取得を足す
5. 未定義コマンドは必ず拒否する

## 変更対象

- `src/net/admin_server.c`
- `src/include/admin_server.h`
- `src/usr/lib/libc/*` または parser 用の新規ファイル
- `src/kernel.c`
- `tests/test_admin_parser.c`
- `src/test/run_qemu_server_smoke.py`

## 完了条件

- [x] `PING` / `STATUS` / `AGENT START` / `AGENT STOP` が通る
- [x] 任意コマンド実行なしで運用に必要な制御ができる
- [x] 未知コマンドを安全に拒否できる
