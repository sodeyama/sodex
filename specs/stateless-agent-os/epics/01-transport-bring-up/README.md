# EPIC 01: Transport Bring-up

## 目的

既存の uIP/NE2000/ソケット層を土台に、固定IPのモックサーバへ安定して TCP 接続し、
平文HTTPの往復ができる状態まで持っていく。

## スコープ

- `connect/send/recv/close` の安定化
- 固定IP向けの最小 HTTP POST/GET bring-up
- QEMU からホスト側モックサーバへの結合確認

## 含めないもの

- DNS
- TLS
- SSE
- Claude/MCP/OAuth

## 実装ステップ

1. `src/socket.c` と `src/net/netmain.c` の送受信経路を棚卸しし、接続確立、再送、切断の穴を洗う
2. 固定IP前提の最小クライアント経路を用意し、HTTPリクエスト文字列を送れることを確認する
3. ホスト側に単純な echo / mock HTTP サーバを置き、QEMU から応答本文を受信させる
4. `kernel.c` かテスト用エントリで bring-up 専用の起動経路を作る
5. 失敗時に `TCP接続失敗`, `送信失敗`, `応答タイムアウト` を切り分けて出せるようにする

## 変更対象

- 既存
  - `src/socket.c`
  - `src/net/netmain.c`
  - `src/kernel.c`
  - `src/test/run_qemu_ktest.py`
- 新規候補
  - `src/net/http_raw_client.c`
  - `src/include/http_raw_client.h`
  - `tests/test_http_raw_client.c`

## テスト

- host 単体
  - TCP状態を持たない純粋関数や HTTP 文字列生成の検証
- 結合
  - ホスト側 mock HTTP サーバへ `POST http://10.0.2.2:8080/echo`
- QEMU スモーク
  - 応答本文がシリアルに出る
  - タイムアウト時に終了コードで失敗を返す

## 完了条件

- 固定IP宛て TCP 接続が再現性を持って成功する
- 平文HTTPの 200 応答を受け取って本文表示できる
- DNS/TLS を無効にした状態で原因切り分けができる
- bring-up 用のスモークテストを 1 コマンドで回せる

## 依存と後続

- 依存: `specs/network-driver/`
- 後続: EPIC-02, EPIC-03, EPIC-09

