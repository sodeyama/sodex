# Agent Transport Tasks

`specs/agent-transport/README.md` の Plan 群を着手単位に分解したタスクリスト。

## Phase A: 平文 HTTP + JSON

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [ ] | AT-01 | `kern_connect()` のタイムアウトを PIT tick ベースに書き換える | 01 | なし | イテレーション数ではなく実時間でタイムアウトする |
| [ ] | AT-02 | TCP 接続エラーを種別（timeout/refused/ARP 失敗）で区別する | 01 | AT-01 | エラーコードで切り分けできる |
| [ ] | AT-03 | connect/close サイクルの安定性を確認する | 01 | AT-01, AT-02 | 3 回連続 connect/close が通る |
| [ ] | AT-04 | `http_client.h` にリクエスト/レスポンス構造体とエラーコードを定義する | 02 | なし | ヘッダがコンパイルできる |
| [ ] | AT-05 | `http_build_request()` を実装する | 02 | AT-04 | GET/POST リクエスト文字列を正しく生成 |
| [ ] | AT-06 | `http_parse_response_headers()` を実装する | 02 | AT-04 | ステータス行と Content-Length/Content-Type をパース |
| [ ] | AT-07 | `http_do_request()` を実装する（平文 TCP 版） | 02 | AT-03, AT-05, AT-06 | connect → send → recv → close が 1 関数で回る |
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
| [ ] | AT-23 | BearSSL の I/O コールバックを `kern_send`/`kern_recv` に接続する | 07 | AT-21, AT-22 | BearSSL の `br_sslio_*` が uIP ソケット経由で通信する |
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
