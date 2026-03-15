# Plan 04: HTTP 制御面

## 概要

管理用の text protocol が成立したら、外部 reverse proxy や監視系とつなぎやすいように、最小の HTTP 制御面を追加する。

## 目標

- `GET /healthz`
- `GET /status`
- `POST /agent/start`
- `POST /agent/stop`

を最小セットとして成立させる。

## スコープ

### 初期

- HTTP/1.0 または単純な HTTP/1.1
- keep-alive なしでもよい
- chunked encoding なし
- 小さな request/response のみ

### 後回し

- 大きな body
- streaming response
- public API 化

## 設計判断

- parser は固定長・低機能でよい
- 管理用 text protocol と handler を共有できる形にする
- 返答は plain text か最小 JSON に留める

## 実装ステップ

1. request line / header parser を作る
2. `healthz`, `status` handler を作る
3. `POST` で制御命令を受ける
4. エラー時は固定コードで返す
5. QEMU hostfwd 越しに curl で叩けるようにする

## テスト

- `curl http://127.0.0.1:18080/healthz`
- `curl http://127.0.0.1:18080/status`
- `curl -X POST http://127.0.0.1:18080/agent/start`

## 変更対象

- `src/net/http_server.c`
- `src/net/admin_server.c`
- `src/kernel.c`
- `tests/test_http_server_parser.c`
- `src/test/run_qemu_server_smoke.py`

## 完了条件

- [ ] `healthz` と `status` が取れる
- [ ] `POST` で管理操作ができる
- [ ] host 側の標準的な HTTP client から扱える
