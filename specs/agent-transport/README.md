# Agent Transport — Layer 4 までの実装計画

`docs/research/sodex_agent_stateless_os_report.md` の Phase 1–2 前半に対応する。
Anthropic API を直接叩くところまでの通信基盤を、障害切り分けしやすい順に積み上げる。

## アーキテクチャ方針

**全コンポーネントをユーザランド（ユーザ空間プロセス）で構築する。**

カーネル側には最小限の改修のみ行い（ソケットタイムアウト、エラーコード、setsockopt syscall）、
HTTP/JSON/DNS/TLS/SSE/Claude adapter は全てユーザ空間のライブラリ＋プロセスとして実装する。

既存のユーザ空間基盤:
- ソケット syscall ラッパー一式（socket/connect/send/recv/sendto/recvfrom/close）
- 基本 libc（strlen, memcpy, strcmp, malloc/free, atoi, printf, htons/ntohs 等）
- debug_write, get_kernel_tick, sleep_ticks syscall

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
| 00 | [ユーザランド基盤整備](plans/00-userland-foundation.md) | libc 拡張、カーネルソケット改修、setsockopt syscall | なし |
| 01 | [能動 TCP 接続](plans/01-active-tcp-client.md) | ユーザランドからの TCP connect スモークテスト | 00 |
| 02 | [HTTP/1.1 クライアント](plans/02-http-client.md) | リクエスト生成、レスポンスパース、Content-Length 受信 | 01 |
| 03 | [JSON パーサ](plans/03-json-parser.md) | トークナイザ、最小 DOM パーサ、JSON ライター | 00 |
| 04 | [平文 HTTP 結合テスト](plans/04-plaintext-http-smoke.md) | ホスト側モックと JSON echo の往復確認 | 01, 02, 03 |
| 05 | [DNS リゾルバ](plans/05-dns-resolver.md) | UDP DNS クエリ、A レコード解決 | 00 |
| 06 | [エントロピーと PRNG](plans/06-entropy-prng.md) | PIT ジッタ収集、AES-CTR PRNG のシード改善 | なし |
| 07 | [BearSSL 移植](plans/07-bearssl-port.md) | libc スタブ、I/O コールバック、最小ビルド | 01, 06 |
| 08 | [TLS クライアント](plans/08-tls-client.md) | BearSSL 上の HTTPS 接続、証明書ピンニング | 02, 05, 07 |
| 09 | [SSE パーサ](plans/09-sse-parser.md) | `data:` 行パース、断片受信、イベント再構成 | 02 |
| 10 | [Claude API アダプタ](plans/10-claude-adapter.md) | Messages API リクエスト/レスポンス、tool_use パース | 03, 08, 09 |
| 11 | [Claude ストリーミング結合](plans/11-claude-streaming-smoke.md) | 実 API またはモックでのストリーミングデモ | 全 Plan |

## 実装順序

```
Phase 0: 基盤整備 (Plan 00)
  00 ユーザランド基盤
    ├── 00-A: libc 拡張（printf %d, snprintf, strstr, strncasecmp, strtol, debug_printf）
    ├── 00-B: カーネルソケット改修（タイムアウト時間ベース化、エラーコード、バッファ拡張）
    └── 00-C: setsockopt syscall 追加

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

- カーネル改修: `src/socket.c`, `src/include/socket.h`, `src/syscall.c`, `src/include/sys/syscalldef.h`
- ユーザ空間 libc: `src/usr/lib/libc/`, `src/usr/include/`
- ユーザ空間新規: `src/usr/lib/libagent/` (HTTP, JSON, DNS, TLS, SSE, Claude adapter)
- ユーザ空間コマンド: `src/usr/command/agent.c` (最終的な agent プロセス)
- テスト: `tests/` 配下に host 単体テスト、`tests/fixtures/` にサンプルデータ
- 外部: `src/usr/lib/bearssl/` に BearSSL ソースの最小サブセット

## TASKS.md

`TASKS.md` で各 Plan の完了状態を追跡する。
