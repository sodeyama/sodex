# Plan 01: 受動 TCP 基盤

## 概要

`sodex` を server として使う最初の関門は、受信側 TCP 接続を受け入れて socket に割り当てること。
現状は `bind()` / `listen()` まではあるが、`accept()` が未実装なので、ここを埋める。

## 目標

- `listen()` 中のポートに対して TCP SYN を受けられる
- 新しい接続ごとに child socket を作れる
- `accept()` が child socket を返せる
- remote address / remote port を保持できる
- close 後に socket がリークしない

## 設計

### 必要な状態

- listening socket
- pending accept queue
- child socket の `SOCK_STATE_CONNECTED`
- listening socket と child socket の対応

### 実装方針

1. `uIP` の passive open を検知する
2. `uip_connected()` が inbound 接続だった場合、listening socket を検索する
3. 空き socket を child として確保する
4. `uip_conn->appstate` を child socket に結びつける
5. listening socket の backlog に child を積む
6. `accept()` は backlog から child を取り出して返す

## 実装ステップ

1. `socket.h` に listening backlog と pending child 管理を定義する
2. `kern_accept()` を blocking 版で実装する
3. `uip_appcall()` で inbound connect を child socket に割り当てる
4. remote address / port を `sockaddr_in` に保存する
5. timeout と close の扱いを整理する
6. close 時に listening / child の双方で整合性を保つ

## テスト

### host 側

- backlog 操作
- socket 状態遷移
- close 後の再利用

### QEMU 側

- host から guest へ TCP 接続
- `accept()` 成功
- 受信データを child socket で読める
- 複数接続を順番に処理できる

## 変更対象

- `src/socket.c`
- `src/include/socket.h`
- `src/net/uip-conf.c`
- `src/usr/include/sys/socket.h`
- `tests/test_socket_server.c`
- `src/test/run_qemu_server_smoke.py`

## 完了条件

- [x] `accept()` が child socket を返す
- [x] inbound TCP で `recv()` まで通る
- [x] close 後に新しい接続を再度受けられる
