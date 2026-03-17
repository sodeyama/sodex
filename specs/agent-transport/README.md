# Agent Transport — 通信基盤からエージェントループまでの実装計画

`docs/research/sodex_agent_stateless_os_report.md` の Phase 1–2 に対応する。
Anthropic API を直接叩く通信基盤を構築し（Phase 0–C、完了済み）、
その上に Claude Agent SDK の設計パターンに基づく自律エージェントを実装する（Phase D–I）。

参考: `docs/research/claude_agent_sdk_integration_research_2026-03-17.md`

## アーキテクチャ方針

**全コンポーネントをユーザランド（ユーザ空間プロセス）で構築する。**

カーネル側には最小限の改修のみ行い（ソケットタイムアウト、エラーコード、setsockopt syscall）、
HTTP/JSON/DNS/TLS/SSE/Claude adapter は全てユーザ空間のライブラリ＋プロセスとして実装する。

既存のユーザ空間基盤:
- ソケット syscall ラッパー一式（socket/connect/send/recv/sendto/recvfrom/close）
- 基本 libc（strlen, memcpy, strcmp, malloc/free, atoi, printf, htons/ntohs 等）
- debug_write, get_kernel_tick, sleep_ticks syscall

## ゴール

### Phase 0–C（通信基盤、完了済み）
- QEMU 上の Sodex からホスト側モックサーバへ平文 HTTP POST/応答の往復ができる
- JSON のパースと生成ができる
- DNS で `api.anthropic.com` を名前解決できる
- BearSSL 上の TLS 1.2 で HTTPS 接続ができる
- SSE ストリーミングを受信してパースできる
- Claude API に対して Messages API を叩き、ストリーミング応答を表示できる

### Phase D–G（エージェント機能）
- Claude の tool_use に応答してユーザランドのツールを実行できる
- マルチターン会話で文脈を維持した対話ができる
- エージェントループで自律的にタスクを完了できる
- セッションの永続化と再開ができる
- フック・権限システムでツール実行を制御できる

### Phase I（対話 UX と継続メモリー）
- `agent` を既定で対話 REPL とし、起動中に会話を継続できる
- セッションを `--continue` / `--resume` で再開できる
- user-scope `/etc/CLAUDE.md` と project-scope `${cwd}/CLAUDE.md` を読んで指示として反映できる
- `AGENTS.md` / `CLAUDE.md` / workspace memory を自動ロードできる
- context 上限時に compaction/checkpoint で重要状態を保てる

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
| 12 | [ツール実行エンジン](plans/12-tool-execution.md) | tool_use ディスパッチ、ツールレジストリ、5 ツール実装 | 10, 11 |
| 13 | [マルチターン会話](plans/13-multi-turn-conversation.md) | 会話履歴管理、tool_result 返送、chat コマンド | 10, 12 |
| 14 | [エージェントループ](plans/14-agent-loop.md) | 自律実行ループ、停止条件、エラーリカバリ | 12, 13 |
| 15 | [システムプロンプトとツール設計](plans/15-system-prompt-and-tools.md) | プロンプト最適化、ツール description 調整、統計 | 12, 14 |
| 16 | [セッション永続化](plans/16-session-persistence.md) | JSONL 保存、セッション再開、容量管理 | 13, 14 |
| 17 | [フックと権限管理](plans/17-hooks-and-permissions.md) | PreToolUse フック、権限ポリシー、監査ログ | 14, 15 |
| 18 | [エージェント結合テスト](plans/18-agent-integration-test.md) | 5 シナリオの E2E テスト、パフォーマンス計測 | 12–17 |
| 19 | [Agent CLI と run_command 強化](plans/19-agent-cli-and-run-command.md) | agent コマンドの CLI 化、run_command の execve+pipe 実装 | 14, 15 |
| 20 | [対話モードと継続メモリー](plans/20-interactive-repl-and-memory.md) | Claude Code / Codex 風 REPL、resume、memory、compaction | 13, 16, 17, 19 |

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

Phase C: Claude 統合 (Plan 09–11) ✅ 完了
  09 SSEパーサ ─┐
               ├→ 10 Claudeアダプタ → 11 ストリーミング結合
  08 TLS ──────┘

Phase D: ツール実行 (Plan 12–13)
  12 ツール実行エンジン → 13 マルチターン会話

Phase E: エージェントループ (Plan 14–15)
  14 エージェントループ → 15 システムプロンプトとツール設計
                         (12, 13 に依存)

Phase F: 永続化と制御 (Plan 16–17)
  16 セッション永続化 ─┐
                      ├→ 17 フックと権限管理
  15 ツール設計 ───────┘

Phase G: 結合 (Plan 18)
  18 エージェント結合テスト (12–17 の全 Plan)

Phase H: 実用化 (Plan 19)
  19 Agent CLI と run_command 強化 (14, 15 に依存)

Phase I: 対話 UX と継続メモリー (Plan 20)
  20 Interactive REPL + resume + memory + compaction
     (13, 16, 17, 19 に依存)
```

## 変更対象（横断）

### Phase 0–C（完了済み）
- カーネル改修: `src/socket.c`, `src/include/socket.h`, `src/syscall.c`, `src/include/sys/syscalldef.h`
- ユーザ空間 libc: `src/usr/lib/libc/`, `src/usr/include/`
- ユーザ空間新規: `src/usr/lib/libagent/` (HTTP, JSON, DNS, TLS, SSE, Claude adapter)
- ユーザ空間コマンド: `src/usr/command/agent.c`, `ask.c`, `claude.c`
- テスト: `tests/` 配下に host 単体テスト、`tests/fixtures/` にサンプルデータ
- 外部: `src/usr/lib/bearssl/` に BearSSL ソースの最小サブセット

### Phase D–G（新規）
- ツール実装: `src/usr/lib/libagent/tools/` (read_file, write_file, list_dir, run_command, get_system_info, manage_process)
- エージェント: `src/usr/lib/libagent/agent.c`, `conversation.c`, `session.c`, `hooks.c`, `permissions.c`, `audit.c`
- ヘッダ: `src/usr/include/agent/` (agent.h, conversation.h, tool_registry.h, tool_dispatch.h, session.h, hooks.h, permissions.h, audit.h)
- コマンド: `src/usr/command/chat.c` (対話型マルチターン)
- 設定: `src/rootfs-overlay/etc/agent/` (system_prompt.txt, permissions.conf, agent.conf)
- テスト: `tests/test_tool_dispatch.c`, `test_conversation.c`, `test_agent_loop.c`, `test_session.c`, `test_hooks_permissions.c`

### Phase I（新規）
- 対話 UX: `src/usr/lib/libagent/repl.c`, `memory_store.c`, `compaction.c`
- コマンド: `src/usr/command/agent.c`（REPL 既定、`-p`, `run`, `--continue`, `--resume`）
- 起動時指示: `src/rootfs-overlay/etc/CLAUDE.md`, `${cwd}/CLAUDE.md`
- memory: `/var/agent/memory/`, `AGENTS.md`, `CLAUDE.md`
- テスト: `tests/test_agent_repl_cli.c`, `test_session_restore_full.c`, `test_memory_loader.c`, `test_compaction.c`

## TASKS.md

`TASKS.md` で各 Plan の完了状態を追跡する。
