# Server Runtime Tasks

`specs/server-runtime/README.md` を、着手単位とフォローアップに分けたタスクリスト。
2026-03-15 時点で、spec 本体は完了、以下は次の運用・製品化向けの残タスク。

## M0: spec 本体

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SRT-01 | passive TCP の inbound accept / backlog / close 整合を実装する | なし | `accept()` が child socket を返し、再接続も通る |
| [x] | SRT-02 | `QEMU user net + hostfwd` で guest の HTTP / admin port に到達できるようにする | SRT-01 | host `127.0.0.1:18080` と `127.0.0.1:10023` から guest へ届く |
| [x] | SRT-03 | text protocol の `PING` / `STATUS` / `AGENT START` / `AGENT STOP` / `LOG TAIL` を実装する | SRT-01, SRT-02 | shell 非公開の named action だけで運用制御できる |
| [x] | SRT-04 | HTTP の `GET /healthz` / `GET /status` / `POST /agent/start` / `POST /agent/stop` を実装する | SRT-02, SRT-03 | 標準的な HTTP client から扱える |
| [x] | SRT-05 | token / role / allowlist / audit ring buffer を入れて制御面を絞る | SRT-03, SRT-04 | 認証なしの制御操作を拒否し、重要操作が記録される |
| [x] | SRT-06 | host test と QEMU smoke で server runtime を固定し、`SSH` は当面 `no-go` と判断する | SRT-01, SRT-02, SRT-03, SRT-04, SRT-05 | spec の完了条件がすべて閉じる |

## M1: 残フォローアップ

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SRT-07 | build-time 固定 token / allowlist を runtime 注入へ置き換える | SRT-05 | `src/makefile` の固定 secret なしで `/etc/sodex-admin.conf` から起動時設定できる |
| [ ] | SRT-08 | Docker / headless 常駐の起動導線を実装する | SRT-02 | Linux + Docker 上で guest server を常駐起動できる |
| [ ] | SRT-09 | 認証失敗に対する rate limit を追加する | SRT-05 | timeout だけでなく失敗連打も抑止できる |
| [ ] | SRT-10 | `test-qemu-server` を継続実行しやすい CI / 運用導線へ寄せる | SRT-06 | 手元だけでなく定期回帰として回せる |
