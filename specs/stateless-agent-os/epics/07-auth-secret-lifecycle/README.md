# EPIC 07: 認証とシークレット管理

## 目的

初期 bring-up で使った固定資格情報から脱却し、起動時注入、メモリ上のみの保持、
短命トークン、更新失敗時のフェイルクローズまでを設計する。

## スコープ

- 起動時シークレット注入
- メモリ上のみのシークレットストア
- 期限付きトークン管理
- OAuth 2.1 相当のトークン取得経路
- 認証失敗時の安全側停止

## 実装ステップ

1. ブート時に受け取る最小資格情報の形式を定義する
2. シークレットを平文のグローバル変数に置かないストア API を作る
3. `expires_at`, `refresh_before`, `scope` を持つトークン表現を定義する
4. 外部 SaaS MCP と LLM API で資格情報の差分を吸収できる adapter を用意する
5. OAuth 2.1 自体はベアメタル側で全部持たず、必要なら token broker を経由する案も残す
6. 更新失敗時は自動で広権限キーへフォールバックせず、失敗として停止する

## 変更対象

- 新規候補
  - `src/security/secret_store.c`
  - `src/include/security/secret_store.h`
  - `src/security/token_manager.c`
  - `src/include/security/token_manager.h`
  - `src/security/oauth_broker_client.c`
  - `src/include/security/oauth_broker_client.h`
  - `tests/test_secret_store.c`
  - `tests/test_token_manager.c`
- 既存
  - `src/agent/claude_adapter.c`
  - `src/mcp/mcp_client.c`

## テスト

- host 単体
  - 期限判定
  - refresh 判定
  - zeroize 後の再利用拒否
- モック結合
  - token broker 成功/失敗
  - 期限切れトークン
  - scope 不足

## 完了条件

- シークレットが永続ファイルへ落ちない
- LLM/MCP の資格情報を共通ライフサイクルで管理できる
- 更新失敗時にフェイルクローズする
- 後続の Ephemeral Memory と自然に接続できる

## 依存と後続

- 依存: EPIC-04, EPIC-06
- 後続: EPIC-08, EPIC-10

