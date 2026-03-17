# Agent Transport — Layer 4 までの実装計画

`docs/research/sodex_agent_stateless_os_report.md` の Phase 1–2 前半に対応する。
Anthropic API を直接叩くところまでの通信基盤を、障害切り分けしやすい順に積み上げる。

## ゴール

- QEMU 上の Sodex からホスト側モックサーバへ平文 HTTP POST/応答の往復ができる
- JSON のパースと生成ができる
- DNS で `api.anthropic.com` を名前解決できる
- BearSSL 上の TLS 1.2 で HTTPS 接続ができる
- SSE ストリーミングを受信してパースできる
- Claude API に対して Messages API を叩き、ストリーミング応答を表示できる

## 前提

- `specs/server-runtime/` の受動 TCP / サーバ側は安定済み
- `specs/stateless-agent-os/` の全体 EPIC 構成に従う
- 初期段階では MCP / OAuth / Capability は入れない

## 制約

- 外部ライブラリ不可（libc なし、`-nostdlib -ffreestanding`）
- BearSSL のみ例外として移植する（malloc 不要設計のため適合する）
- i486 32bit、AES-NI なし → ChaCha20-Poly1305 を優先暗号スイートにする

## Plan 一覧

| # | Plan | 概要 | 主な依存 |
|---|------|------|---------|
| 01 | [能動 TCP 接続](plans/01-active-tcp-client.md) | `kern_connect()` の安定化、能動 TCP のスモークテスト | server-runtime |
| 02 | [HTTP/1.1 クライアント](plans/02-http-client.md) | リクエスト生成、レスポンスパース、Content-Length 受信 | 01 |
| 03 | [JSON パーサ](plans/03-json-parser.md) | トークナイザ、最小 DOM パーサ、JSON ライター | なし |
| 04 | [平文 HTTP 結合テスト](plans/04-plaintext-http-smoke.md) | ホスト側モックと JSON echo の往復確認 | 01, 02, 03 |
| 05 | [DNS リゾルバ](plans/05-dns-resolver.md) | UDP DNS クエリ、A レコード解決 | 01 |
| 06 | [エントロピーと PRNG](plans/06-entropy-prng.md) | PIT ジッタ収集、AES-CTR PRNG のシード改善 | なし |
| 07 | [BearSSL 移植](plans/07-bearssl-port.md) | libc スタブ、I/O コールバック、最小ビルド | 01, 06 |
| 08 | [TLS クライアント](plans/08-tls-client.md) | BearSSL 上の HTTPS 接続、証明書ピンニング | 02, 05, 07 |
| 09 | [SSE パーサ](plans/09-sse-parser.md) | `data:` 行パース、断片受信、イベント再構成 | 02 |
| 10 | [Claude API アダプタ](plans/10-claude-adapter.md) | Messages API リクエスト/レスポンス、tool_use パース | 03, 08, 09 |
| 11 | [Claude ストリーミング結合](plans/11-claude-streaming-smoke.md) | 実 API またはモックでのストリーミングデモ | 全 Plan |

## 実装順序

```
Phase A: 平文 HTTP + JSON (Plan 01–04)
  01 能動TCP → 02 HTTPクライアント → 03 JSONパーサ → 04 平文結合テスト

Phase B: HTTPS 基盤 (Plan 05–08)
  05 DNS ─┐
  06 PRNG ┼→ 07 BearSSL移植 → 08 TLSクライアント
         ─┘

Phase C: Claude 統合 (Plan 09–11)
  09 SSEパーサ ─┐
               ├→ 10 Claudeアダプタ → 11 ストリーミング結合
  08 TLS ──────┘
```

## 変更対象（横断）

- 既存: `src/socket.c`, `src/net/netmain.c`, `src/kernel.c`, `makefile.inc`
- 新規: `src/net/http_client.c`, `src/lib/json.c`, `src/net/dns.c`, `src/net/tls_client.c`, `src/net/sse_parser.c`, `src/agent/claude_adapter.c`
- テスト: `tests/` 配下に host 単体テスト、`tests/fixtures/` にサンプルデータ
- 外部: `src/lib/bearssl/` に BearSSL ソースの最小サブセット

## TASKS.md

着手時に `TASKS.md` を作成し、各 Plan の完了状態を追跡する。
