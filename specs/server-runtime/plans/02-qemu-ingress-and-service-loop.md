# Plan 02: QEMU 受信経路と常駐サービスループ

## 概要

cloud 上の Docker/QEMU 構成で server として使うには、guest の待受ポートに host から到達できる必要がある。
また、単発テストではなく常駐サービスとして `network_poll()` を安定して回す必要がある。

## 目標

- host から guest の管理ポートへ到達できる
- headless QEMU で常駐実行できる
- service loop が busy-poll に偏りすぎず、他処理と共存できる

## 受信経路

### Phase 1

- `QEMU user net + hostfwd`
- 例:
  - host `127.0.0.1:18080` -> guest `10.0.2.15:8080`
  - host `127.0.0.1:10023` -> guest `10.0.2.15:10023`

この方式は cloud 上で扱いやすく、tap/bridge より簡単。

### Phase 2

- 必要なら tap/bridge を検討
- ただし初期の server bring-up には不要

## サービスループ

### 方針

- `network_poll()` を timer ベースで確実に回す
- server 側待機処理が CPU を燃やしすぎないようにする
- guest 内の Agent loop と server loop を競合させない

### 実装候補

- カーネルメインループに service tick を入れる
- `accept()` / `recv()` の待機は busy-wait だけでなく timeout を明示する
- シリアルログは必要最小限に絞る

## テスト

- headless QEMU 常駐起動
- hostfwd 越しの接続試験
- 連続接続での安定性確認
- idle 時の CPU 使用率観測

## 変更対象

- `bin/start.sh` または cloud 用の新規起動スクリプト
- `src/kernel.c`
- `src/net/netmain.c`
- `src/socket.c`
- `src/test/run_qemu_server_smoke.py`

## 完了条件

- [x] host から guest 管理ポートへ到達できる
- [x] headless 実行で server 機能が壊れない
- [x] idle 時の無駄な busy loop を把握できる
