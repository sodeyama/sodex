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

## 詳細残タスク

### SRT-08: Docker / headless 常駐の起動導線

2026-03-15 時点で、Docker image / entrypoint / `server-headless` 導線までは作成済み。
ただし Linux + Docker 上の close path がまだ不安定で、常駐導線としては未完。

- [x] Docker 用の `Dockerfile` / `entrypoint.sh` / `bin/start.sh` を用意する
- [x] Linux gcc の PIE 差分を吸収し、container 内 build を通す
- [x] stale object を避ける fresh build 手順を確認する
  - `/tmp/sodex-server-runtime-build` を作り直すと `admin_poll_connection` の frame は `0x58`、`http_poll_connection` は `0x78` まで縮む
- [x] `kern_send()` / `socket_begin_close()` で `uip_poll_conn()` 再入を避ける
- [x] `kern_close_socket()` の close wait 中に `socket_table[sockfd]` を引き直す
- [ ] headless 起動後の ready 条件を定義する
  - 現状は serial に `listener ready` を出す前に host/container から叩くと timeout しやすい
- [ ] admin port (`10023`) の request/response を Docker 経由で安定させる
  - `PING` は run によって `ERR forbidden` が返るケースと timeout するケースがある
- [ ] HTTP port (`18080`) の response 後に guest が落ちないようにする
  - 2026-03-15 の最新観測では `HTTP/1.1 403 Forbidden` を返した直後に `PF: CR2=00 err=00 eip=C0022AA6 cs=08`
- [ ] close/ACK/FIN のどこで制御が壊れるかを特定し、再現テストを固定する
- [ ] Docker 実行時の allowlist 期待値を整理する
  - `allow_ip=10.0.2.2` 前提で通るケースと `403 forbidden` になるケースがあり、hostfwd 経由の peer IP の扱いを固定できていない
- [ ] 常駐運用の完了条件を満たしたら README の「未確認」注記を閉じる

### SRT-09: 認証失敗 rate limit

- [ ] text protocol と HTTP の両方で失敗回数を peer 単位に数える
- [ ] token 不一致、token なし、allowlist 不一致をどう同一 bucket に入れるか決める
- [ ] 固定 sleep ではなく、時間窓 + backoff で抑止する
- [ ] throttle 時も audit ring buffer に残す
- [ ] host test を追加する
  - token mismatch の連打
  - allowlist 外からの連打
  - throttle 後の回復

### SRT-10: `test-qemu-server` の CI / 継続運用導線

- [ ] host 手元 smoke と Docker/headless smoke を分けて定義する
- [ ] ready 待ちを serial 依存で固定し、早すぎる probe で false negative にならないようにする
- [ ] serial log / `qemu_debug.log` / Docker log を artifact として保存する
- [ ] `PF:` や boot 失敗を検知したら即 fail する
- [ ] 定期実行しやすい entrypoint を `make` か CI workflow に寄せる
- [ ] `SRT-08` が閉じるまでは Docker/headless job を advisory 扱いにするかを決める
