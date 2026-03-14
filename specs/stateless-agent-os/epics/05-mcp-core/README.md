# EPIC 05: MCPプロトコルコア

## 目的

Claude adapter の次に、MCP 通信そのものを独立層として実装する。
JSON-RPC 2.0、`initialize`、`tools/list`、`resources/read`、`tools/call` を
個別に検証できる形で組み立てる。

## スコープ

- JSON-RPC 2.0 request/response
- MCP `initialize`
- `tools/list`
- `resources/read`
- `tools/call`
- エラーコード変換

## 実装ステップ

1. JSON-RPC の `id`, `method`, `params`, `result`, `error` を扱う共通層を作る
2. MCP `initialize` を実装し、サーバ capability と protocol version を取得する
3. `tools/list` と `resources/read` を別 API として切り分ける
4. `tools/call` を実装し、結果の本文とエラーを抽出する
5. MCP 側の失敗を Claude 側へ返せるよう、内部エラー表現を揃える
6. この段階ではまだ Capability 制御を前提にしつつ、実行制御自体は後段へ渡す

## 変更対象

- 新規候補
  - `src/mcp/jsonrpc_client.c`
  - `src/include/mcp/jsonrpc_client.h`
  - `src/mcp/mcp_client.c`
  - `src/include/mcp/mcp_client.h`
  - `tests/test_mcp_adapter.c`
  - `tests/fixtures/mcp/`
- 既存
  - `src/lib/json.c`
  - `tests/Makefile`

## テスト

- host 単体
  - JSON-RPC request 生成
  - `result` と `error` の解析
  - `tools/list` / `resources/read` の fixture
- モック結合
  - ローカル MCP mock サーバ
  - 不正な `id`、未知メソッド、タイムアウト

## 完了条件

- MCP の各操作を Claude adapter から独立して呼べる
- `initialize` と `tools/list` の情報を構造体で保持できる
- MCP error を内部エラーコードへ変換できる
- 次段の Capability 制御を差し込める API 境界がある

## 依存と後続

- 依存: EPIC-02, EPIC-04
- 後続: EPIC-06, EPIC-07, EPIC-09

