# EPIC 09: 観測性とテスト基盤

## 目的

API 仕様変更、通信断、QEMU 上の不安定挙動を後追いで調べるのではなく、
最初から fixture、モック、シリアル診断、QEMU スモークで検出できるようにする。

## スコープ

- adapter fixture テスト
- mock HTTP / Claude / MCP サービス
- Phase 別 QEMU スモーク
- シリアルログとデバッグフラグ
- CI へ組み込みやすいテスト実行導線

## 実装ステップ

1. `tests/fixtures/` を Claude/MCP/HTTP ごとに整理する
2. ホスト上で動く mock サーバ群を用意する
3. QEMU スモークを Phase 単位に分ける
   - Phase 1: 平文HTTP
   - Phase 2: Claude HTTPS/SSE
   - Phase 3: MCP 許可/拒否
4. 送受信ログ、TLS失敗、JSON parse error、Capability拒否をシリアルへ出す
5. `make test` と別に `make test-qemu-agent` のような入口を整える

## 変更対象

- 新規候補
  - `tests/test_claude_adapter.c`
  - `tests/test_mcp_adapter.c`
  - `tests/test_sse_parser.c`
  - `tests/test_capability.c`
  - `tests/fixtures/http/`
  - `tests/fixtures/claude/`
  - `tests/fixtures/mcp/`
  - `src/test/mock_http_server.py`
  - `src/test/mock_claude_server.py`
  - `src/test/mock_mcp_server.py`
  - `src/test/run_qemu_agent_smoke.py`
- 既存
  - `tests/Makefile`
  - `src/test/run_qemu_ktest.py`

## テスト

- host 単体
  - parser / adapter / capability
- 結合
  - mock サーバを使った protocol-level テスト
- QEMU
  - Phase 別 smoke
  - タイムアウト、途中切断、429、拒否ケース

## 完了条件

- 新しい adapter を追加したら fixture と mock テストを必ず増やせる
- QEMU スモークが段階別に走る
- ログだけで失敗層を特定できる
- 研究レポートのリスク項目をテストケースへ落とし込める

## 依存と後続

- 依存: 横断
- 後続: EPIC-10

