# EPIC 06: Capability SecurityとAgent Loop

## 目的

MCP を「つながる」状態から「安全に使える」状態へ進める。
未許可の接続先やツールを実行しない `deny-by-default` のゲートを先に作り、
その上に Claude の `tool_use` を載せる。

## スコープ

- Capability モデル
- タスクごとの許可集合
- MCP 接続先 whitelist
- `tool_use` parser と dispatch
- Observe → Think → Act → Cleanup の基本ループ

## 実装ステップ

1. `server`, `tool`, `resource`, `verb` を表現する Capability 構造体を定義する
2. タスク開始時にロードされる Capability セットを定義し、未指定は拒否する
3. MCP 接続先を whitelist で制限し、未知 URL への接続を拒否する
4. Claude の `tool_use` を内部 command に変換する
5. `tool_use` 実行前に `server whitelist` と `tool capability` を両方検査する
6. 成功時は `tool_result` を LLM に返し、失敗時は拒否理由を明示して返す

## 変更対象

- 新規候補
  - `src/agent/capability.c`
  - `src/include/agent/capability.h`
  - `src/agent/tool_dispatch.c`
  - `src/include/agent/tool_dispatch.h`
  - `src/agent/agent_loop.c`
  - `src/include/agent/agent_loop.h`
  - `tests/test_capability.c`
  - `tests/test_tool_dispatch.c`
- 既存
  - `src/agent/claude_adapter.c`
  - `src/mcp/mcp_client.c`

## テスト

- host 単体
  - Capability 判定
  - `tool_use` から内部 command への変換
- モック結合
  - 許可済みツール成功
  - 未許可 server/tool の拒否
- QEMU スモーク
  - `read_file` のような単純ツール成功
  - 拒否ケースで安全に停止

## 完了条件

- 未許可の MCP 接続先やツールが実行されない
- Claude `tool_use` から `tool_result` まで 1 ループ回る
- 拒否理由がログとモデルの両方に見える
- OAuth 未実装でも管理下 MCP だけでデモできる

## 依存と後続

- 依存: EPIC-05
- 後続: EPIC-07, EPIC-08, EPIC-10

