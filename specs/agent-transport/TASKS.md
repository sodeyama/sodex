# Agent Transport Tasks

`specs/agent-transport/README.md` の Plan 群を着手単位に分解したタスクリスト。

**方針**: 全コンポーネントをユーザランドで構築する。

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
| [ ] | AT-01 | ユーザランドから TCP connect/send/recv/close のサイクルを確認する | 01 | Phase 0 | ユーザ空間プロセスから `10.0.2.2:8080` に接続してデータ往復 |
| [ ] | AT-02 | connect/close サイクルを 3 回繰り返して安定性を確認する | 01 | AT-01 | socket リークなし |
| [ ] | AT-03 | 接続エラー時にエラーコードで切り分けできることを確認する | 01 | AT-01 | timeout/refused がユーザランドで判別できる |
| [ ] | AT-04 | `http_client.h` にリクエスト/レスポンス構造体とエラーコードを定義する | 02 | なし | ヘッダがコンパイルできる |
| [ ] | AT-05 | `http_build_request()` を実装する | 02 | AT-04, AT-P00-02 | GET/POST リクエスト文字列を正しく生成 |
| [ ] | AT-06 | `http_parse_response_headers()` を実装する | 02 | AT-04, AT-P00-04, AT-P00-05 | ステータス行と Content-Length/Content-Type をパース |
| [ ] | AT-07 | `http_do_request()` を実装する（平文 TCP 版） | 02 | AT-01, AT-05, AT-06 | connect → send → recv → close が 1 関数で回る |
| [ ] | AT-08 | JSON トークナイザとツリービルダを実装する | 03 | なし | 6 型をパースしてトークン配列に格納 |
| [ ] | AT-09 | JSON アクセサ（`json_find_key`, `json_array_get` 等）を実装する | 03 | AT-08 | ネスト構造のフィールドアクセスができる |
| [ ] | AT-10 | JSON ライター（`jw_*` 系）を実装する | 03 | なし | Claude API リクエストボディを生成できる |
| [ ] | AT-11 | JSON の host 単体テストを書いて通す | 03 | AT-08, AT-09, AT-10 | fixture のパースとライター出力が正しい |
| [ ] | AT-12 | HTTP の host 単体テストを書いて通す | 02 | AT-05, AT-06 | fixture のリクエスト生成とレスポンスパースが正しい |
| [ ] | AT-13 | モック HTTP サーバ (`tests/mock_http_server.py`) を作成する | 04 | なし | echo, healthz, mock/claude エンドポイントが動く |
| [ ] | AT-14 | QEMU 平文 HTTP 結合テスト (`make test-agent-bringup`) を通す | 04 | AT-07, AT-11, AT-13 | 固定 IP + JSON POST の往復が 1 コマンドで確認できる |

## Phase B: HTTPS 基盤

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [ ] | AT-15 | DNS クエリ構築とラベルエンコードを実装する | 05 | なし | FQDN → DNS ワイヤフォーマット変換が正しい |
| [ ] | AT-16 | DNS 応答パース（A レコード、ラベル圧縮対応）を実装する | 05 | AT-15 | fixture の応答バイナリから IPv4 を取得できる |
| [ ] | AT-17 | `dns_resolve()` をブロッキング版で実装する | 05 | AT-15, AT-16 | QEMU 上で `api.anthropic.com` を解決できる |
| [ ] | AT-18 | DNS キャッシュを実装する | 05 | AT-17 | 同一ホスト名の 2 回目はクエリを送らない |
| [ ] | AT-19 | エントロピープール（PIT ジッタ収集）を実装する | 06 | なし | 起動後 1 秒以内に 256 bit 以上収集 |
| [ ] | AT-20 | PRNG を統合し、既存 SSH の PRNG を新エントロピーで初期化する | 06 | AT-19 | SSH の既存機能が壊れない |
| [ ] | AT-21 | BearSSL ソースの最小サブセットを配置し、クロスコンパイルを通す | 07 | なし | `-m32 -nostdlib` でビルドエラーなし |
| [ ] | AT-22 | libc スタブ（`memmove` 等）を BearSSL 向けに用意する | 07 | AT-21 | 未解決シンボルなしでリンクが通る |
| [ ] | AT-23 | BearSSL の I/O コールバックを `send_msg`/`recv_msg` に接続する | 07 | AT-21, AT-22 | BearSSL の `br_sslio_*` がユーザ空間ソケット経由で通信する |
| [ ] | AT-24 | BearSSL に PRNG コールバックを接続する | 07 | AT-20, AT-23 | TLS ハンドシェイクに暗号学的乱数が使われる |
| [ ] | AT-25 | `tls_connect()` / `tls_send()` / `tls_recv()` / `tls_close()` を実装する | 08 | AT-23, AT-24, AT-17 | FQDN 指定で TLS 接続が成立する |
| [ ] | AT-26 | 証明書ピンニングを実装する | 08 | AT-25 | `api.anthropic.com` のピンが通る |
| [ ] | AT-27 | `http_do_request()` に TLS 分岐を追加する | 08 | AT-25, AT-07 | `use_tls=1` で HTTPS リクエストが送れる |
| [ ] | AT-28 | ホスト側 TLS サーバとの QEMU スモークテストを通す | 08 | AT-27 | TLS 上で HTTP GET → 200 を受信 |

## Phase C: Claude 統合

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [ ] | AT-29 | SSE パーサの行バッファリングとイベント dispatch を実装する | 09 | なし | 断片受信でイベント再構成できる |
| [ ] | AT-30 | SSE の host 単体テストを書いて通す | 09 | AT-29 | fixture の SSE ストリームが正しくパースされる |
| [ ] | AT-31 | Claude API Config 構造体（endpoint, headers）を定義する | 10 | なし | テーブル駆動の設定ファイルがコンパイルできる |
| [ ] | AT-32 | `claude_build_request()` を実装する | 10 | AT-10, AT-31 | Messages API リクエスト JSON を生成できる |
| [ ] | AT-33 | `claude_parse_sse_event()` を実装する | 10 | AT-08, AT-29 | SSE の data JSON から内部表現に変換できる |
| [ ] | AT-34 | tool_use パースと `claude_needs_tool_call()` を実装する | 10 | AT-33 | tool_use の id/name/input を取得できる |
| [ ] | AT-35 | `claude_build_tool_result()` を実装する | 10 | AT-10 | tool_result メッセージの JSON を生成できる |
| [ ] | AT-36 | Claude adapter の host 単体テストを書いて通す | 10 | AT-32, AT-33, AT-34 | fixture の生成・パースが正しい |
| [ ] | AT-37 | `llm_provider` 抽象化インターフェースを定義し Claude を登録する | 10 | AT-32, AT-33 | 関数ポインタテーブル経由で Claude adapter が呼べる |
| [ ] | AT-38 | 429/5xx のリトライとバックオフを実装する | 10 | AT-33 | リトライ後に成功レスポンスを受け取れる |
| [ ] | AT-39 | 高レベル `claude_send_message()` を実装する | 11 | AT-27, AT-33, AT-37 | 1 関数で DNS→TLS→HTTP→SSE→パースが回る |
| [ ] | AT-40 | モック Claude サーバを TLS + SSE 対応に拡張する | 11 | AT-13 | TLS 自己署名 + SSE ストリーミングを返す |
| [ ] | AT-41 | `make test-claude-smoke` で結合テストを通す | 11 | AT-39, AT-40 | テキスト応答と tool_use 応答の 2 シナリオが PASS |
| [ ] | AT-42 | 全レイヤーの診断ログフォーマットを統一する | 11 | AT-41 | DNS/TCP/TLS/HTTP/SSE/CLAUDE の各段階がシリアルに出る |
