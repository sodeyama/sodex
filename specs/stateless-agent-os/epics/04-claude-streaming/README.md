# EPIC 04: Claudeストリーミング統合

## 目的

MCP をまだ入れず、Claude API 単独で `HTTPS + HTTP + SSE` の経路を完成させる。
まずはモデルとの対話だけを通し、ツール実行は後続 EPIC に分離する。

## スコープ

- Claude API 向けリクエスト adapter
- SSE parser
- ストリーミング表示
- 429/5xx/切断時のエラー処理

## 実装ステップ

1. Claude API の request body 生成を adapter 化する
2. SSE レコードを `event` / `data` 単位で組み立て、断片受信を扱う
3. `message_start`, `content_block_delta`, `message_stop`, `error` の主要イベントを処理する
4. 表示層を最小限に保ち、まずはシリアル出力、その後 VGA 出力へ広げる
5. `tool_use` はこの段階ではパースだけ行い、実行しない
6. レート制限と API エラーの表示フォーマットを統一する

## 変更対象

- 新規候補
  - `src/agent/claude_adapter.c`
  - `src/include/agent/claude_adapter.h`
  - `src/net/sse_parser.c`
  - `src/include/sse_parser.h`
  - `tests/test_claude_adapter.c`
  - `tests/test_sse_parser.c`
  - `tests/fixtures/claude/`
- 既存
  - `src/kernel.c`
  - `tests/Makefile`

## テスト

- host 単体
  - Claude request body 生成
  - SSE レコード再構成
  - `tool_use` を含む応答 parser
- モック結合
  - SSE 分割送信
  - 429 と 500 応答
- QEMU スモーク
  - 単純なプロンプトに対してストリーミング応答が出る

## 完了条件

- Claude API へ HTTPS で POST できる
- SSE の断片受信でも応答を復元できる
- `tool_use` を後段に渡すための内部表現を持つ
- MCP 未実装でも Claude 単独デモが成立する

## 依存と後続

- 依存: EPIC-03
- 後続: EPIC-05, EPIC-06, EPIC-07, EPIC-09

