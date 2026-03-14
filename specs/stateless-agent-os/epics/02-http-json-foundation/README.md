# EPIC 02: HTTP/JSON基盤

## 目的

EPIC-01 の bring-up 実装を一発物で終わらせず、以後の Claude/MCP adapter が共有できる
HTTP/1.1 と JSON の最小共通基盤に整理する。

## スコープ

- HTTP/1.1 リクエスト生成
- レスポンスヘッダの解析
- Content-Length ベース受信
- 最小 JSON tokenizer / parser
- バッファ制限とエラーコード設計

## 実装ステップ

1. HTTPメッセージ生成を `method`, `host`, `path`, `headers`, `body` で構築する API に切り出す
2. レスポンスヘッダのステータス行、主要ヘッダ、ボディ開始位置を解析する
3. chunked 未対応を明示し、現段階では `Content-Length` 必須で失敗させる
4. JSON の tokenizer と最小 parser を用意し、Claude/MCP が必要とする型だけ先に扱う
5. すべての parser で `入力不足`, `バッファ超過`, `未知ヘッダ`, `不正JSON` を区別する

## 変更対象

- 新規候補
  - `src/net/http_client.c`
  - `src/include/http_client.h`
  - `src/lib/json.c`
  - `src/include/json.h`
  - `tests/test_http_client.c`
  - `tests/test_json_parser.c`
  - `tests/fixtures/http/`
- 既存
  - `tests/Makefile`

## テスト

- host 単体
  - HTTP リクエスト生成
  - ステータス行とヘッダ解析
  - JSON の object/array/string/number/bool/null
- fixture
  - 正常応答
  - ヘッダ欠落
  - 部分受信
  - 大きすぎる本文

## 完了条件

- HTTP と JSON が Claude/MCP で共通利用できる API になっている
- 失敗理由が数値または列挙で返る
- `tests/test_http_client.c` と `tests/test_json_parser.c` が回る
- 後続 EPIC が raw な文字列組み立てに戻らない

## 依存と後続

- 依存: EPIC-01
- 後続: EPIC-03, EPIC-04, EPIC-05, EPIC-09

