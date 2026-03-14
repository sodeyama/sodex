# Stateless Agent OS Spec

`docs/research/sodex_agent_stateless_os_report.md` を、実装可能な EPIC 群に分解した計画。
既存の `specs/network-driver/` を土台に、`固定IP + 平文HTTP` から順に積み上げる。

## ゴール

- QEMU 上の Sodex から Claude API へ HTTPS/SSE で接続できる
- Claude の `tool_use` を受けて、許可済み MCP にだけ安全にアクセスできる
- 資格情報と一時データをメモリ上でのみ扱い、タスク完了後に安全に消去できる
- API 仕様変更や通信断に対して、原因を切り分けやすいテストと診断手段を持つ

## 前提

1. `specs/network-driver/` のネットワーク層が先行して安定していること
2. 初期段階では `DNS/TLS/MCP/OAuth` を同時に入れないこと
3. MCP 実行前に `deny-by-default` の接続制御と Capability チェックを入れること
4. 外部 SaaS MCP の認証は bring-up 完了後に段階導入すること

## ディレクトリ構成

```
specs/stateless-agent-os/
├── README.md
└── epics/
    ├── 01-transport-bring-up/
    ├── 02-http-json-foundation/
    ├── 03-dns-tls-bearssl/
    ├── 04-claude-streaming/
    ├── 05-mcp-core/
    ├── 06-capability-agent-loop/
    ├── 07-auth-secret-lifecycle/
    ├── 08-ephemeral-memory/
    ├── 09-observability-and-test/
    └── 10-reliability-and-autonomy/
```

## EPIC一覧

| # | EPIC | 概要 | 主な依存 |
|---|------|------|---------|
| 01 | [Transport Bring-up](epics/01-transport-bring-up/README.md) | 固定IP向け TCP と平文HTTP bring-up | `specs/network-driver/` |
| 02 | [HTTP/JSON基盤](epics/02-http-json-foundation/README.md) | 再利用可能な HTTP/1.1 と JSON の整備 | 01 |
| 03 | [DNS/TLS/BearSSL](epics/03-dns-tls-bearssl/README.md) | DNS, PRNG, BearSSL, 証明書検証 | 01, 02 |
| 04 | [Claudeストリーミング](epics/04-claude-streaming/README.md) | SSE と Claude adapter の実装 | 03 |
| 05 | [MCPコア](epics/05-mcp-core/README.md) | JSON-RPC 2.0 と MCP 基本操作 | 02, 04 |
| 06 | [CapabilityとAgent Loop](epics/06-capability-agent-loop/README.md) | tool_use, whitelist, deny-by-default 実行 | 05 |
| 07 | [認証とシークレット管理](epics/07-auth-secret-lifecycle/README.md) | 起動時注入、短命トークン、更新 | 04, 06 |
| 08 | [Ephemeral Memory](epics/08-ephemeral-memory/README.md) | 暗号化バッファ、鍵管理、消去 | 06, 07 |
| 09 | [観測性とテスト](epics/09-observability-and-test/README.md) | fixture, モック, QEMU スモーク, ログ | 横断 |
| 10 | [信頼性と自律運用](epics/10-reliability-and-autonomy/README.md) | バックオフ、ウォッチドッグ、フェイルクローズ | 06, 07, 08, 09 |

## 実装順序

1. EPIC-01 で既存 TCP/ソケット層の不安定要素を落とし、固定IP先の平文HTTPを通す
2. EPIC-02 で HTTP と JSON をアプリケーション層の再利用部品にする
3. EPIC-03 で DNS/TLS を追加し、HTTPS 経路を成立させる
4. EPIC-04 で Claude API 単独統合を完了する
5. EPIC-05 と EPIC-06 で MCP 実行経路を組み、未許可ツールを拒否する
6. EPIC-07 と EPIC-08 で資格情報と一時データを安全化する
7. EPIC-09 と EPIC-10 で検証と運用耐性を固める

## 横断ルール

- TLS bring-up 完了前は外部 API への本番接続を前提にしない
- OAuth 導入前はローカルまたは管理下の MCP だけを対象にする
- 新しい adapter は必ず host 単体テストと QEMU スモークの両方を持つ
- 失敗時は「自動で広い権限にフォールバック」せず、常に安全側に倒す

## 共通の変更候補

- 既存: `src/socket.c`, `src/net/netmain.c`, `src/net/uip-conf.c`, `src/kernel.c`, `tests/Makefile`
- 新規候補: `src/net/http_*`, `src/net/dns.c`, `src/net/tls_*`, `src/agent/*`, `src/mcp/*`, `tests/fixtures/*`, `tests/mocks/*`

