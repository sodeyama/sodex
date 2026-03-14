# EPIC 10: 信頼性と自律運用

## 目的

通信断、レート制限、トークン更新失敗、MCP 障害時に、安全側へ倒れつつ自律的に回復できる
最小運用モデルを整える。

## スコープ

- リトライと指数バックオフ
- ウォッチドッグ
- フェイルクローズ
- タスク予算とタイムアウト
- 将来の Memory MCP / 複数 MCP への拡張余地

## 実装ステップ

1. HTTP/TLS/MCP の各層で再試行可能な失敗と即失敗すべき失敗を分類する
2. LLM API 429、5xx、接続断に対するバックオフ方針を実装する
3. タスク単位の時間予算、試行回数上限、メモリ上限を持たせる
4. ハング検出用のウォッチドッグを追加し、再起動時の安全側初期化を定義する
5. 認証や Capability 失敗はリトライせず、フェイルクローズする
6. 将来の Memory MCP や複数 MCP 並行実行のために scheduler 境界を設計する

## 変更対象

- 新規候補
  - `src/agent/retry_policy.c`
  - `src/include/agent/retry_policy.h`
  - `src/agent/task_budget.c`
  - `src/include/agent/task_budget.h`
  - `src/agent/watchdog.c`
  - `src/include/agent/watchdog.h`
  - `tests/test_retry_policy.c`
- 既存
  - `src/agent/agent_loop.c`
  - `src/security/token_manager.c`
  - `src/mcp/mcp_client.c`
  - `src/kernel.c`

## テスト

- host 単体
  - retry/backoff 計算
  - task budget 判定
- モック結合
  - 429 の再試行
  - 認証失敗の即停止
  - MCP timeout の上限制御
- QEMU スモーク
  - 通信断からの復帰
  - ハング時の watchdog 反応

## 完了条件

- 再試行してよい失敗と即停止すべき失敗が分かれている
- Agent Loop が無限再試行で固まらない
- watchdog と task budget が暴走を止められる
- 複数 MCP や長期記憶を後から載せる境界が設計されている

## 依存と後続

- 依存: EPIC-06, EPIC-07, EPIC-08, EPIC-09
- 後続: 将来の長期記憶、複数 MCP 並行実行
