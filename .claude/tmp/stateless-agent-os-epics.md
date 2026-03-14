# Stateless Agent OS EPICタスクリスト

このファイルは `docs/research/sodex_agent_stateless_os_report.md` を `specs/` に落とし込むための
一時タスクリスト。EPICの洗い出し、依存関係整理、spec化の進捗管理に使う。

## 前提

- 既存の `specs/network-driver/` で整備した uIP/NE2000/ソケット層を土台にする
- まずは `固定IP + 平文HTTP` で bring-up し、その後 `TLS + Claude単独`、最後に `MCP` を載せる
- 外部MCPの認証と短命トークン運用は bring-up 後に段階導入する

## EPIC一覧

- [x] EPIC-01: Transport Bring-up
  固定IPのモックサーバへTCP接続し、平文HTTP往復を安定化する
  依存: `specs/network-driver/`

- [x] EPIC-02: HTTP/JSONアプリケーション基盤
  HTTP/1.1、ヘッダ生成、Content-Length、最小JSONパーサを整備する
  依存: EPIC-01

- [x] EPIC-03: DNS/TLS/BearSSL基盤
  DNS名前解決、エントロピー、BearSSL移植、証明書検証/ピンニングを実装する
  依存: EPIC-01, EPIC-02

- [x] EPIC-04: Claudeストリーミング統合
  SSEパーサとClaude adapterを実装し、HTTPS経由で応答表示まで通す
  依存: EPIC-03

- [x] EPIC-05: MCPプロトコルコア
  JSON-RPC 2.0、MCP initialize/tools/resources、エラーハンドリングを実装する
  依存: EPIC-02, EPIC-04

- [x] EPIC-06: Capability SecurityとAgent Loop
  deny-by-defaultのMCP接続制御、Capabilityチェック、tool_useディスパッチを実装する
  依存: EPIC-05

- [x] EPIC-07: 認証とシークレット管理
  起動時シークレット注入、短命トークン管理、OAuth 2.1相当の更新フローを実装する
  依存: EPIC-04, EPIC-06

- [x] EPIC-08: Ephemeral MemoryとCrypto Erasure
  暗号化バッファ、鍵管理、ゼロ化、タスク完了時クリーンアップを実装する
  依存: EPIC-06, EPIC-07

- [x] EPIC-09: 観測性とテスト基盤
  fixture単体テスト、モックサーバ結合テスト、Phase別QEMUスモークを整備する
  依存: EPIC-01 以降の全EPICに横断

- [x] EPIC-10: 信頼性と自律運用
  バックオフ、ウォッチドッグ、失敗時フェイルクローズ、将来の複数MCP運用を計画する
  依存: EPIC-06, EPIC-07, EPIC-08, EPIC-09

## specs反映タスク

- [x] `specs/stateless-agent-os/README.md` を作成
- [x] 各EPICごとのディレクトリと README を作成
- [x] 各EPICの配下を詳細READMEとして計画化
- [x] 依存関係が README 間で整合していることを確認
- [x] このタスクリストを spec 作成後の状態に更新
