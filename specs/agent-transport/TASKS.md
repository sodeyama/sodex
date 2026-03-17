# Agent Transport Tasks

`specs/agent-transport/README.md` の Plan 群を着手単位に分解したタスクリスト。

**方針**: 全コンポーネントをユーザランドで構築する。
**設計参考**: Claude Agent SDK のアーキテクチャ（`docs/research/claude_agent_sdk_integration_research_2026-03-17.md`）

## Phase 0: ユーザランド基盤整備

### 00-A: libc 拡張

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-P00-01 | `printf` に `%d` と `%u` フォーマット指定子を追加する | 00 | なし | `printf("%d", -42)` で `-42` が出力される |
| [x] | AT-P00-02 | `vsnprintf()` / `snprintf()` を実装する | 00 | AT-P00-01 | バッファに `%s`, `%d`, `%u`, `%x`, `%c` をフォーマット出力できる |
| [x] | AT-P00-03 | `strstr()` を実装する | 00 | なし | `strstr("foo\r\n\r\nbar", "\r\n\r\n")` が正しい位置を返す |
| [x] | AT-P00-04 | `strncasecmp()` を実装する | 00 | なし | `strncasecmp("Content-Length", "content-length", 14)` が 0 を返す |
| [x] | AT-P00-05 | `strtol()` を実装する | 00 | なし | `strtol("12345", NULL, 10)` が 12345 を返す。16進数にも対応 |
| [x] | AT-P00-06 | `strcat()` / `strncat()` を実装する | 00 | なし | 文字列連結が正しく動作する |
| [x] | AT-P00-07 | `debug_printf()` を実装する（debug_write syscall 経由でシリアル出力） | 00 | AT-P00-02 | `debug_printf("tick=%d\n", 100)` がシリアルに出力される |
| [x] | AT-P00-08 | libc 拡張の host 単体テストを書いて通す | 00 | AT-P00-01〜07 | snprintf, strstr, strncasecmp, strtol の全テストが PASS |

### 00-B: カーネルソケット改修

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-P00-09 | ソケットエラーコードを `socket.h` に定義する | 00 | なし | SOCK_ERR_TIMEOUT, SOCK_ERR_REFUSED, SOCK_ERR_ARP_FAIL 等が定義される |
| [x] | AT-P00-10 | `kern_connect()` のタイムアウトを PIT tick ベースに書き換える | 00 | AT-P00-09 | イテレーション数ではなく実時間（10秒）でタイムアウトする |
| [x] | AT-P00-11 | `kern_connect()` でエラー種別（timeout/refused/ARP失敗）を区別する | 00 | AT-P00-09, AT-P00-10 | uIP の状態から適切なエラーコードが返る |
| [x] | AT-P00-12 | `kern_recvfrom()` のタイムアウトを PIT tick ベースに書き換える | 00 | なし | `timeout_ticks` フィールドの値で実時間タイムアウトする |
| [x] | AT-P00-13 | `kern_close_socket()` のクローズ待ちを PIT tick ベースにする | 00 | なし | イテレーション数ではなく実時間でクローズ待ちが終わる |
| [x] | AT-P00-14 | `SOCK_RXBUF_SIZE` を 4096 → 8192 に拡張する | 00 | AT-P00-14a | 8192 で ktest 24/24 PASS |
| [x] | AT-P00-14a | ktest ビルドで `init_paging()` を `run_kernel_tests()` 前に呼ぶ | 00 | なし | PF の根本原因: ktest が初期ブートページテーブルのまま動作していた。`init_paging()` を `init_mem()` 直後に移動して解決 |
| [x] | AT-P00-14b | startup.S に `first_pg_tbl3` を追加し初期マップを 8MB → 12MB に拡張する | 00 | なし | 将来の BSS 増大への保険。`first_pg_tbl3` 追加、PDE[2]/PDE[770] 設定 |
| [x] | AT-P00-15 | `kern_sendto()` (TCP) で MSS 超のデータを分割送信する | 00 | なし | 2000 バイトのデータを 1 回の kern_send() で送れる |

### 00-C: setsockopt syscall

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-P00-16 | カーネル側 `kern_setsockopt()` を実装する | 00 | AT-P00-12 | SO_RCVTIMEO でソケットの recv タイムアウトを設定できる |
| [x] | AT-P00-17 | `SYS_CALL_SETSOCKOPT` (414) を syscalldef.h と syscall.c に登録する | 00 | AT-P00-16 | syscall テーブルに登録される |
| [x] | AT-P00-18 | ユーザ空間の syscall ラッパー `setsockopt.S` を追加する | 00 | AT-P00-17 | ユーザ空間から `setsockopt()` が呼べる |
| [x] | AT-P00-19 | `sys/socket.h` に `setsockopt()` 宣言と `SO_RCVTIMEO` を追加する | 00 | AT-P00-18 | ヘッダをインクルードしてコンパイルできる |
| [x] | AT-P00-20 | QEMU スモークテスト: setsockopt, 分割送信, connect/close サイクル確認 | 00 | AT-P00-10〜19 | ktest 24/24 PASS（setsockopt_rcvtimeo, tcp_split_send_2000, tcp_split_recv_echo, connect_close_3_cycles） |

## Phase A: 平文 HTTP + JSON

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-01 | ユーザランドから TCP connect/send/recv/close のサイクルを確認する | 01 | Phase 0 | ユーザ空間プロセスから `10.0.2.2:8080` に接続してデータ往復 |
| [x] | AT-02 | connect/close サイクルを 3 回繰り返して安定性を確認する | 01 | AT-01 | socket リークなし |
| [x] | AT-03 | 接続エラー時にエラーコードで切り分けできることを確認する | 01 | AT-01 | timeout/refused がユーザランドで判別できる |
| [x] | AT-04 | `http_client.h` にリクエスト/レスポンス構造体とエラーコードを定義する | 02 | なし | ヘッダがコンパイルできる |
| [x] | AT-05 | `http_build_request()` を実装する | 02 | AT-04, AT-P00-02 | GET/POST リクエスト文字列を正しく生成 |
| [x] | AT-06 | `http_parse_response_headers()` を実装する | 02 | AT-04, AT-P00-04, AT-P00-05 | ステータス行と Content-Length/Content-Type をパース |
| [x] | AT-07 | `http_do_request()` を実装する（平文 TCP 版） | 02 | AT-01, AT-05, AT-06 | connect → send → recv → close が 1 関数で回る |
| [x] | AT-08 | JSON トークナイザとツリービルダを実装する | 03 | なし | 6 型をパースしてトークン配列に格納 |
| [x] | AT-09 | JSON アクセサ（`json_find_key`, `json_array_get` 等）を実装する | 03 | AT-08 | ネスト構造のフィールドアクセスができる |
| [x] | AT-10 | JSON ライター（`jw_*` 系）を実装する | 03 | なし | Claude API リクエストボディを生成できる |
| [x] | AT-11 | JSON の host 単体テストを書いて通す | 03 | AT-08, AT-09, AT-10 | fixture のパースとライター出力が正しい |
| [x] | AT-12 | HTTP の host 単体テストを書いて通す | 02 | AT-05, AT-06 | fixture のリクエスト生成とレスポンスパースが正しい |
| [x] | AT-13 | モック HTTP サーバ (`tests/mock_http_server.py`) を作成する | 04 | なし | echo, healthz, mock/claude エンドポイントが動く |
| [x] | AT-14 | QEMU 平文 HTTP 結合テスト (`make test-agent-bringup`) を通す | 04 | AT-07, AT-11, AT-13 | 固定 IP + JSON POST の往復が 1 コマンドで確認できる |

## Phase B: HTTPS 基盤

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-15 | DNS クエリ構築とラベルエンコードを実装する | 05 | なし | FQDN → DNS ワイヤフォーマット変換が正しい |
| [x] | AT-16 | DNS 応答パース（A レコード、ラベル圧縮対応）を実装する | 05 | AT-15 | fixture の応答バイナリから IPv4 を取得できる |
| [x] | AT-17 | `dns_resolve()` をブロッキング版で実装する | 05 | AT-15, AT-16 | QEMU 上で `api.anthropic.com` を解決できる |
| [x] | AT-18 | DNS キャッシュを実装する | 05 | AT-17 | 同一ホスト名の 2 回目はクエリを送らない |
| [x] | AT-19 | エントロピープール（PIT ジッタ収集）を実装する | 06 | なし | 起動後 1 秒以内に 256 bit 以上収集 |
| [x] | AT-20 | PRNG を統合し、既存 SSH の PRNG を新エントロピーで初期化する | 06 | AT-19 | SSH の既存機能が壊れない |
| [x] | AT-21 | BearSSL ソースの最小サブセットを配置し、クロスコンパイルを通す | 07 | なし | `-m32 -nostdlib` でビルドエラーなし |
| [x] | AT-22 | libc スタブ（`memmove` 等）を BearSSL 向けに用意する | 07 | AT-21 | 未解決シンボルなしでリンクが通る |
| [x] | AT-23 | BearSSL の I/O コールバックを `send_msg`/`recv_msg` に接続する | 07 | AT-21, AT-22 | BearSSL の `br_sslio_*` がユーザ空間ソケット経由で通信する |
| [x] | AT-24 | BearSSL に PRNG コールバックを接続する | 07 | AT-20, AT-23 | TLS ハンドシェイクに暗号学的乱数が使われる |
| [x] | AT-25 | `tls_connect()` / `tls_send()` / `tls_recv()` / `tls_close()` を実装する | 08 | AT-23, AT-24, AT-17 | FQDN 指定で TLS 接続が成立する |
| [x] | AT-26 | 証明書ピンニングを実装する | 08 | AT-25 | x509 novalidate + decoder で公開鍵抽出（ピンニングは後続で強化） |
| [x] | AT-27 | `http_do_request()` に TLS 分岐を追加する | 08 | AT-25, AT-07 | `use_tls=1` で HTTPS リクエストが送れる |
| [x] | AT-28 | ホスト側 TLS サーバとの QEMU スモークテストを通す | 08 | AT-27 | TLS 上で HTTP GET → 200 を受信 |

## Phase C: Claude 統合

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-29 | SSE パーサの行バッファリングとイベント dispatch を実装する | 09 | なし | 断片受信でイベント再構成できる |
| [x] | AT-30 | SSE の host 単体テストを書いて通す | 09 | AT-29 | fixture の SSE ストリームが正しくパースされる |
| [x] | AT-31 | Claude API Config 構造体（endpoint, headers）を定義する | 10 | なし | テーブル駆動の設定ファイルがコンパイルできる |
| [x] | AT-32 | `claude_build_request()` を実装する | 10 | AT-10, AT-31 | Messages API リクエスト JSON を生成できる |
| [x] | AT-33 | `claude_parse_sse_event()` を実装する | 10 | AT-08, AT-29 | SSE の data JSON から内部表現に変換できる |
| [x] | AT-34 | tool_use パースと `claude_needs_tool_call()` を実装する | 10 | AT-33 | tool_use の id/name/input を取得できる |
| [x] | AT-35 | `claude_build_tool_result()` を実装する | 10 | AT-10 | tool_result メッセージの JSON を生成できる |
| [x] | AT-36 | Claude adapter の host 単体テストを書いて通す | 10 | AT-32, AT-33, AT-34 | fixture の生成・パースが正しい |
| [x] | AT-37 | `llm_provider` 抽象化インターフェースを定義し Claude を登録する | 10 | AT-32, AT-33 | 関数ポインタテーブル経由で Claude adapter が呼べる |
| [x] | AT-38 | 429/5xx のリトライとバックオフを実装する | 10 | AT-33 | リトライ後に成功レスポンスを受け取れる |
| [x] | AT-39 | 高レベル `claude_send_message()` を実装する | 11 | AT-27, AT-33, AT-37 | 1 関数で DNS→TLS→HTTP→SSE→パースが回る |
| [x] | AT-40 | モック Claude サーバを TLS + SSE 対応に拡張する | 11 | AT-13 | TLS 自己署名 + SSE ストリーミングを返す |
| [x] | AT-41 | `make test-claude-smoke` で結合テストを通す | 11 | AT-39, AT-40 | テキスト応答と tool_use 応答の 2 シナリオが PASS |
| [x] | AT-42 | 全レイヤーの診断ログフォーマットを統一する | 11 | AT-41 | DNS/TCP/TLS/HTTP/SSE/CLAUDE の各段階がシリアルに出る |

## Phase D: ツール実行 (Plan 12–13)

### Plan 12: ツール実行エンジン

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-43 | `tool_registry.h` にレジストリ API（登録、検索、列挙）を定義する | 12 | なし | ヘッダがコンパイルできる |
| [x] | AT-44 | `tool_registry.c` にレジストリ実装（固定配列ベース）を作成する | 12 | AT-43 | ツールの登録と名前検索が動作する |
| [x] | AT-45 | `tool_dispatch.h` / `tool_dispatch.c` にディスパッチャを実装する | 12 | AT-44 | tool_use ブロックから対応ハンドラが呼ばれる |
| [x] | AT-46 | `tool_read_file.c` を実装する（ext3fs からファイル読み取り） | 12 | AT-45 | JSON 入力のパスからファイル内容を tool_result に返せる |
| [x] | AT-47 | `tool_write_file.c` を実装する（ext3fs へのファイル書き込み） | 12 | AT-45 | JSON 入力のパスと内容でファイルを作成/上書きできる |
| [x] | AT-48 | `tool_list_dir.c` を実装する（ext3fs ディレクトリ一覧） | 12 | AT-45 | ディレクトリ内のファイル名とサイズを tool_result に返せる |
| [x] | AT-49 | `tool_get_system_info.c` を実装する（カーネル情報取得） | 12 | AT-45 | メモリ/プロセス/デバイス情報を JSON で tool_result に返せる |
| [x] | AT-50 | `tool_run_command.c` を実装する（execve + パイプキャプチャ） | 12 | AT-45 | コマンド実行結果の stdout を tool_result に返せる |
| [x] | AT-51 | `claude_build_request_with_tools()` を拡張してツール定義を送信する | 12 | AT-44 | tools 配列付きのリクエスト JSON が正しく生成される |
| [x] | AT-52 | 各ツールの JSON Schema 文字列をコンパイル時定数として定義する | 12 | AT-51 | 全ツールの input_schema が Claude API 仕様に準拠 |
| [ ] | AT-53 | ツール実行の host 単体テスト (`test_tool_dispatch.c`) を書いて通す | 12 | AT-46〜50 | レジストリ、ディスパッチ、エラー処理の全テスト PASS |

### Plan 13: マルチターン会話

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-54 | `conversation.h` に会話データ構造（conv_turn, conv_block, conversation）を定義する | 13 | なし | ヘッダがコンパイルできる |
| [x] | AT-55 | `conversation.c` に `conv_init`, `conv_add_user_text`, `conv_add_assistant_response` を実装する | 13 | AT-54 | ユーザーとアシスタントのターンを追加できる |
| [x] | AT-56 | `conv_add_tool_results()` を実装する（複数 tool_result 対応） | 13 | AT-55 | 1 つの user ターンに複数の tool_result ブロックを追加できる |
| [x] | AT-57 | `conv_build_messages_json()` を実装する（全ターンの JSON 化） | 13 | AT-55, AT-56 | text, tool_use, tool_result の混在ターンを正しい JSON に変換 |
| [x] | AT-58 | `claude_send_conversation()` を `claude_client.c` に追加する | 13 | AT-57, AT-39 | 全会話履歴を含むリクエストで API 応答を受信できる |
| [x] | AT-59 | トークン追跡と閾値チェック（`conv_check_tokens`）を実装する | 13 | AT-58 | トークン使用量が加算され、閾値超過を検知できる |
| [x] | AT-60 | 古いターン切り捨てロジックを実装する | 13 | AT-59 | 閾値超過時に最古のターンから削除される |
| [x] | AT-61 | `chat` コマンドを実装する（対話型マルチターン） | 13 | AT-58 | agent REPL モードとして統合実装済み（独立 chat コマンドではなく agent の既定動作） |
| [ ] | AT-62 | マルチターン会話の host 単体テスト (`test_conversation.c`) を書いて通す | 13 | AT-57 | JSON 化、tool_result 統合、トークン管理のテスト PASS |
| [x] | AT-63 | モックサーバに tool_use → tool_result → 再応答のシナリオを追加する | 13 | AT-58 | 2 往復のマルチターンが QEMU で動作する |

## Phase E: エージェントループ (Plan 14–15)

### Plan 14: エージェントループ

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-64 | `agent.h` にエージェント設定・状態・結果の構造体を定義する | 14 | なし | ヘッダがコンパイルできる |
| [x] | AT-65 | `agent_loop.c` に `agent_run()` メインループを実装する | 14 | AT-64, AT-58, AT-45 | prompt → API → ツール実行 → API → ... → 最終応答のループ |
| [x] | AT-66 | `agent_step()` を切り出してテスタブルにする | 14 | AT-65 | 1 ステップ単位でテスト可能 |
| [x] | AT-67 | 停止条件を実装する: end_turn, max_steps, specific_tool, token_limit | 14 | AT-65 | 各停止条件で正しく停止する |
| [x] | AT-68 | エラーリカバリを実装する（API 再試行、ツールエラー時 is_error 返送） | 14 | AT-65 | ツールエラー時に Claude にフィードバックされ、エージェントは継続する |
| [x] | AT-69 | 診断ログの各ステップ出力を実装する | 14 | AT-65 | [AGENT] step N/M フォーマットでシリアルに出力される |
| [x] | AT-70 | `agent` コマンドに `agent run` サブコマンドを追加する | 14 | AT-65 | `agent run "タスク"` で自律実行が起動する |
| [x] | AT-71 | 統計サマリ（ステップ数、トークン、ツール呼び出し回数、実行時間）を実装する | 14 | AT-65 | エージェント終了時にサマリが出力される |
| [x] | AT-72 | エージェントループの結合テスト (`agent_integ.c`) を書いて通す | 14 | AT-66 | 8 シナリオ（即完、1 ツール、2 連鎖、max_steps、REPL、session resume、memory、compact）が PASS |
| [x] | AT-73 | モックサーバに 4 シナリオ（即完、1 ツール、2 連鎖、max_steps）を追加する | 14 | AT-65 | QEMU で 4 シナリオが PASS |

### Plan 15: システムプロンプトとツール設計

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-74 | `system_prompt.txt` にシステムプロンプト草案を作成する | 15 | なし | Environment, Capabilities, Guidelines, Constraints の 4 セクションを含む |
| [x] | AT-75 | 全ツールの JSON Schema を最適化された description 付きで定義する | 15 | AT-52 | Claude が各ツールの用途を正しく理解できる |
| [x] | AT-76 | `tool_read_file.c` に bounded output 対応を追加する | 15 | AT-46 | 大きなファイルの head/tail 抜粋ができる |
| [ ] | AT-77 | `tool_manage_process.c` を新規実装する（list, info, kill, nice） | 15 | AT-45 | プロセス管理の 4 アクションが動作する |
| [x] | AT-78 | `/etc/agent/` のファイル群を rootfs-overlay に配置する | 15 | AT-74 | ビルド時に system_prompt.txt と agent.conf が rootfs に含まれる |
| [x] | AT-79 | `agent_load_config()` でファイルからプロンプトと設定を読み込む | 15 | AT-78, AT-65 | /etc/agent/ から設定が読み込まれる |
| [ ] | AT-80 | ツール統計のモニタリングと出力を実装する | 15 | AT-71 | ツール別の呼び出し回数、成功率、平均時間が出力される |

## Phase F: 永続化と制御 (Plan 16–17)

### Plan 16: セッション永続化

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-81 | `session.h` にセッション管理 API を定義する | 16 | なし | ヘッダがコンパイルできる |
| [x] | AT-82 | `session_create()` — ID 生成とメタデータ行書き込みを実装する | 16 | AT-81 | `/var/agent/sessions/<id>.jsonl` が作成される |
| [x] | AT-83 | `session_append_turn()` — JSONL 形式での 1 行追記を実装する | 16 | AT-82 | 会話ターンが JSONL ファイルに追記される |
| [x] | AT-84 | `session_load()` — JSONL を読み込んで conversation に復元する | 16 | AT-83, AT-55 | 保存されたセッションから会話が正しく復元される |
| [x] | AT-85 | `session_list()` / `session_delete()` — 一覧と削除を実装する | 16 | AT-82 | セッションファイルの列挙と削除が動作する |
| [x] | AT-86 | `session_cleanup()` — 容量ベースの自動クリーンアップを実装する | 16 | AT-85 | 上限超過時に最古のセッションが削除される |
| [x] | AT-87 | `agent.c` に session 連携を組み込む（start_new_session / resume_session） | 16 | AT-84 | 会話の開始時に自動保存、resume で復元 |
| [x] | AT-88 | `agent.c` のループ内で各ターン後に `session_append_turn()` を追加する | 16 | AT-83, AT-65 | エージェント実行が自動的にセッションに保存される |
| [x] | AT-89 | `agent` コマンドにセッション関連サブコマンドを追加する | 16 | AT-85 | `agent sessions`, `agent --resume <id>`, `agent sessions --delete <id>` が動作する |
| [x] | AT-90 | セッション永続化のテスト (`test_session_restore_full.c`) を書いて通す | 16 | AT-84 | 作成、追記、読み込みの主要ケースが PASS |

### Plan 17: フックと権限管理

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-91 | `hooks.h` にフックシステム API を定義する | 17 | なし | ヘッダがコンパイルできる |
| [x] | AT-92 | `hooks.c` にフック登録・実行エンジンを実装する | 17 | AT-91 | PreToolUse / PostToolUse フックが発火する |
| [x] | AT-93 | `permissions.h` / `permissions.c` に権限チェックロジックを実装する | 17 | なし | ツール名とパスで許可/拒否を判定できる |
| [x] | AT-94 | `perm_load_policy()` — 設定ファイルパーサを実装する | 17 | AT-93 | `/etc/agent/permissions.conf` を読み込んでポリシーが適用される |
| [x] | AT-95 | `audit.h` / `audit.c` — 監査ログの書き込み・読み取りを実装する | 17 | なし | ツール実行が `/var/agent/audit.log` に記録される |
| [x] | AT-96 | `audit_rotate()` — ログローテーションを実装する | 17 | AT-95 | ログサイズ上限で古いエントリが削除される |
| [ ] | AT-97 | `agent_loop.c` のツール実行にフック・権限チェックを統合する | 17 | AT-92, AT-93, AT-65 | ツール実行前に権限チェックとフックが呼ばれる |
| [ ] | AT-98 | ビルトインフック（パス保護 `/boot/*`, `/etc/agent/*`）を登録する | 17 | AT-97 | 保護パスへの write_file がブロックされる |
| [x] | AT-99 | `/etc/agent/permissions.conf` のデフォルト設定を作成する | 17 | AT-94 | standard モードで読み取り許可、危険コマンド拒否 |
| [ ] | AT-100 | `agent audit` サブコマンドで監査ログを閲覧できるようにする | 17 | AT-95 | 監査ログエントリが表示される |
| [ ] | AT-101 | フック・権限の host 単体テスト (`test_hooks_permissions.c`) を書いて通す | 17 | AT-92, AT-93 | フックブロック、権限拒否、監査ログの全テスト PASS |

## Phase G: 結合 (Plan 18)

### Plan 18: エージェント結合テスト

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-102 | `tests/run_agent_integration.py` テストオーケストレーターを作成する | 18 | なし | モック起動 → QEMU → 判定 → クリーンアップのフレームワーク |
| [x] | AT-103 | モックサーバに 8 シナリオのレスポンスパターンを追加する | 18 | AT-73 | immediate, one_tool, two_tools, max_steps, repl_memory, session_resume, memory_loader, continue_compact |
| [x] | AT-104 | シナリオ 1（ファイル探索と報告）を実装・検証する | 18 | AT-65, AT-46, AT-48 | list_dir → read_file の連鎖で最終報告テキストが得られる |
| [x] | AT-105 | シナリオ 2（システム診断）を実装・検証する | 18 | AT-49 | get_system_info → 診断レポートが得られる |
| [x] | AT-106 | シナリオ 3（ファイル作成と検証）を実装・検証する | 18 | AT-47, AT-46 | write_file → read_file → 確認報告が得られる |
| [ ] | AT-107 | シナリオ 4（権限ブロックとリカバリ）を実装・検証する | 18 | AT-98 | 保護パスへの書き込みがブロックされ、Claude が代替パスで成功する |
| [x] | AT-108 | シナリオ 5（セッション再開）を実装・検証する | 18 | AT-88, AT-84 | session resume シナリオが PASS |
| [x] | AT-109 | パフォーマンス計測と出力を実装する | 18 | AT-71 | ステップ数・トークンが agent_print_summary で出力される |
| [x] | AT-110 | `make test-agent-full` ターゲットを作成する | 18 | AT-102 | 1 コマンドで agent 結合テストが実行される |
| [ ] | AT-111 | 実 Claude API での手動デモを確認する | 18 | AT-110 | 実 API でエージェントが自律的にタスクを完了する |
| [x] | AT-112 | Phase A–C のリグレッションテストが PASS することを確認する | 18 | AT-110 | `make test-agent-bringup` と `make test-claude-smoke` が引き続き通る |

## Phase H: 実用化 (Plan 19)

### Plan 19: Agent CLI コマンドと run_command 強化

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-P19-01 | `agent.c` を CLI コマンドに書き換え（テストコード → agent_run 呼び出し） | 19 | AT-65, AT-79 | `agent "list files in /"` で agent_run() が呼ばれる |
| [x] | AT-P19-02 | `tool_run_command.c` に execve+pipe によるコマンド実行を実装 | 19 | AT-50 | `ls /usr/bin` の出力が tool_result に返る |
| [x] | AT-P19-03 | `claude_send_conversation_with_key()` を追加 | 19 | AT-58 | API キー付きで会話送信できる |
| [x] | AT-P19-04 | `agent_loop.c` の `send_conversation()` で API キー対応 | 19 | AT-P19-03 | config.api_key が使われる |
| [x] | AT-P19-05 | ビルド確認と既存テストの regression チェック | 19 | AT-P19-01〜04 | `make` が通り、テストが壊れない |

## Phase I: 対話 UX と継続メモリー (Plan 20)

### Plan 20: 対話モードと継続メモリー

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | AT-P20-01 | `agent` の既定起動を REPL にし、`agent -p` / `agent run` / `agent --continue` / `agent --resume` の CLI 仕様を実装する | 20 | AT-P19-01 | `agent` で対話起動、`agent -p` で単発、`agent run` で従来の自律実行が動く |
| [ ] | AT-P20-02 | `agent_repl_run()` と入出力ループを実装し、初回 prompt 付き REPL と streaming 応答を提供する | 20 | AT-P20-01, AT-65 | `agent "質問"` で 1 ターン目送信後もプロンプトに戻る |
| [x] | AT-P20-03 | slash command (`/help`, `/clear`, `/compact`, `/memory`, `/permissions`, `/resume`, `/sessions`, `/rename`, `/status`) を実装する | 20 | AT-P20-02, AT-94 | 対話中にセッション制御と memory 操作ができる |
| [x] | AT-P20-04 | `!cmd` と `# memory` ショートカットを実装し、実行結果・memory 追記を会話に反映する | 20 | AT-P20-02, AT-P19-02 | `!ls /etc` の結果を次ターンで参照でき、`# build は make` が memory に残る |
| [x] | AT-P20-05 | `session_meta` に `cwd` / `cwd_hash` / `name` / `last_active_at` / `compact_count` を追加し、直近セッション索引を管理する | 20 | AT-81, AT-82 | `--continue` で現在 `cwd` の直近セッションを引ける |
| [x] | AT-P20-06 | `session_append_turn()` を full-fidelity JSONL に更新し、`tool_use` / `tool_result` / `compact` / `rename` event を保存する | 20 | AT-P20-05, AT-83 | 再開に必要な会話状態が JSONL に失われず残る |
| [x] | AT-P20-07 | `session_load()` を full-fidelity restore に更新し、compact summary + recent raw turn で会話を再構成する | 20 | AT-P20-06, AT-84 | 再開後に前回の tool state と未完了論点を LLM が把握できる |
| [x] | AT-P20-08 | user-scope `/etc/CLAUDE.md` seed と project-scope `${cwd}/CLAUDE.md` loader を実装し、両方の読込と `AGENTS.md` / `CLAUDE.md` の上位探索・subtree lazy load を行う | 20 | AT-79, AT-P20-02 | `agent` 起動時に `/etc/CLAUDE.md` と `${cwd}/CLAUDE.md` が順に会話へ注入され、起動時と subtree アクセス時に該当 memory が反映される |
| [ ] | AT-P20-09 | workspace auto memory と secret filter を実装し、`/memory` と `#` から編集できるようにする | 20 | AT-P20-08, AT-95 | 再利用価値のある知識だけが `/var/agent/memory/<cwd-hash>.md` に保存される |
| [x] | AT-P20-10 | `conv_compact()` を実装し、手動 `/compact` と自動 compaction で checkpoint summary を生成する | 20 | AT-P20-07 | context 使用率上昇時に summary + recent turn へ縮約される |
| [ ] | AT-P20-11 | `run_command` / `read_file` の bounded output と artifact path 返却を実装する | 20 | AT-P19-02, AT-P20-10 | 長い出力が head/tail 抜粋 + artifact 参照に変換される |
| [ ] | AT-P20-12 | host 単体テスト (`test_agent_repl_cli.c`, `test_session_restore_full.c`, `test_memory_loader.c`, `test_compaction.c`, `test_bounded_output.c`) を追加する | 20 | AT-P20-11 | REPL・resume・memory・compaction の主要ケースが PASS |
| [ ] | AT-P20-13 | QEMU smoke を追加し、multi-turn → exit → continue → compact の一連動作を検証する | 20 | AT-P20-12 | `agent` の対話継続と前回文脈再開が自動で確認できる |
