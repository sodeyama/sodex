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
| [x] | SRT-08 | Docker / headless 常駐の起動導線を実装する | SRT-02 | Linux + Docker 上で guest server を常駐起動できる |
| [x] | SRT-09 | 認証失敗に対する rate limit を追加する | SRT-05 | timeout だけでなく失敗連打も抑止できる |
| [ ] | SRT-10 | `test-qemu-server` を継続実行しやすい CI / 運用導線へ寄せる | SRT-06 | 手元だけでなく定期回帰として回せる |

## M2: 追加で見えた残タスク

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | SRT-11 | throttle 応答に retry 情報を返す | SRT-09 | HTTP `429` と text protocol の両方で再試行待ち時間を client が受け取れる |
| [ ] | SRT-12 | 起動設定の fail-safe と可視化を整える | SRT-07 | config 不備や token 欠落時の挙動が audit / ready marker / test で固定される |
| [ ] | SRT-13 | server runtime の異常系テストを拡充する | SRT-08, SRT-09, SRT-10 | happy path だけでなく `403` / `429` / `busy` / `timeout` 系も回帰で拾える |
| [ ] | SRT-14 | allowlist を複数 peer / CIDR へ広げる | SRT-07, SRT-08 | Linux host / Docker Desktop / CI runner の差を単一 `allow_ip` 上書きなしで吸収できる |
| [ ] | SRT-15 | debug shell 用の最小 TCP-PTY bridge を実装する | SRT-10, SRT-12, SRT-13 | raw TCP client から token 付きで shell session を張り、切断/再接続まで安定する |
| [ ] | SRT-16 | 暗号込み `SSH server` を段階導入する | SRT-15 | `OpenSSH` client から最小の shell login が通る |

## 詳細残タスク

### SRT-08: Docker / headless 常駐の起動導線

2026-03-15 時点で、Docker image / entrypoint / `server-headless` 導線に加えて、
Docker 経由の HTTP/admin smoke まで安定化できた。

- [x] Docker 用の `Dockerfile` / `entrypoint.sh` / `bin/start.sh` を用意する
- [x] Linux gcc の PIE 差分を吸収し、container 内 build を通す
- [x] stale object を避ける fresh build 手順を確認する
  - `/tmp/sodex-server-runtime-build` を作り直すと `admin_poll_connection` の frame は `0x58`、`http_poll_connection` は `0x78` まで縮む
- [x] `kern_send()` / `socket_begin_close()` で `uip_poll_conn()` 再入を避ける
- [x] `kern_close_socket()` の close wait 中に `socket_table[sockfd]` を引き直す
- [x] headless 起動後の ready 条件を定義する
  - serial/stdout に `AUDIT server_runtime_ready allow_ip=... admin=10023 http=8080` が出た時点を ready とする
- [x] admin port (`10023`) の request/response を Docker 経由で安定させる
  - `PING` / `STATUS` / `LOG TAIL` を含む smoke が published port 経由で通る
- [x] HTTP port (`18080`) の response 後に guest が落ちないようにする
  - `GET /healthz` / `GET /status` / `POST /agent/start` / `POST /agent/stop` の往復後も fault marker なしを確認した
- [x] close/ACK/FIN のどこで制御が壊れるかを特定し、再現テストを固定する
  - server handler 後に pending TCP を flush する `socket_service_pending_tcp()` と `uip_newdata()` 応答条件で close path を安定化した
- [x] Docker 実行時の allowlist 期待値を整理する
  - Linux host の既定は `10.0.2.2`、Docker Desktop/macOS では `192.168.65.1` を確認した
- [x] 常駐運用の完了条件を満たしたら README の「未確認」注記を閉じる
  - README に arm64 host build と allowlist 上書き手順を反映した

2026-03-15 追記:

- `docker/server-runtime/entrypoint.sh` は `/tmp/sodex-server-runtime-build` で fresh build してから headless 起動する
- `make test-docker-server` / `src/test/run_docker_server_smoke.py` を追加し、Docker/headless smoke を別導線で回せるようにした
- `docker/server-runtime/Dockerfile` は `i686-linux-gnu` cross toolchain を使う構成へ更新し、arm64 host でも native container build が通る
- `socket_service_pending_tcp()` を main loop / timer interrupt 後段で回し、HTTP/admin の pending send/close を即座に flush するようにした
- この macOS + Docker Desktop 環境でも `SODEX_ADMIN_ALLOW_IP=192.168.65.1` を付けた smoke が通り、`SRT-08` の closure 条件を満たした

### SRT-09: 認証失敗 rate limit

- [x] text protocol と HTTP の両方で失敗回数を peer 単位に数える
- [x] token 不一致、token なし、allowlist 不一致をどう同一 bucket に入れるか決める
- [x] 固定 sleep ではなく、時間窓 + backoff で抑止する
- [x] throttle 時も audit ring buffer に残す
- [x] host test を追加する
  - token mismatch の連打
  - allowlist 外からの連打
  - throttle 後の回復

2026-03-15 追記:

- rate limit bucket は peer 単位で共有し、allowlist / token なし / token 不一致を同一 peer の失敗として数える
- host test に、allowlist 拒否から token 認証失敗へ遷移しても同じ bucket で throttle されるケースを追加した

### SRT-10: `test-qemu-server` の CI / 継続運用導線

- [x] host 手元 smoke と Docker/headless smoke を分けて定義する
  - `make test-qemu-server` と `make test-docker-server` を分け、runner も `run_qemu_server_smoke.py` / `run_docker_server_smoke.py` に分離した
- [x] ready 待ちを serial 依存で固定し、早すぎる probe で false negative にならないようにする
  - QEMU smoke は serial log 上の `AUDIT server_runtime_ready ...` を待ってから probe する
- [x] serial log / `qemu_debug.log` / Docker log を artifact として保存する
  - QEMU は `server_serial.log` / `server_qemu_debug.log` / `server_monitor.log`、Docker は `docker-container.log` / `docker-guest-log/qemu_debug.log` を残す
- [x] `PF:` や boot 失敗を検知したら即 fail する
  - `PF:` / `PageFault` / `General Protection Exception` を QEMU/Docker smoke の両方で fail 扱いにした
- [x] 定期実行しやすい entrypoint を `make` か CI workflow に寄せる
  - `make docker-server-image` / `make test-docker-server` を追加した
- [x] `SRT-08` が閉じた後の Docker/headless job の扱いを決める
  - `test-docker-server` は advisory ではなく通常の smoke として扱ってよい
- [ ] repo ルートから `test-qemu-server` を叩ける入口を揃える
  - 現状は `src/makefile` 側に target があり、root `makefile` には wrapper がない
- [ ] Linux runner 向け CI workflow/job を追加し、`test-qemu-server` を定期回帰へ載せる
  - repo 直下に CI workflow がまだなく、QEMU smoke は手元実行前提のまま
- [ ] `test-docker-server` を CI job 化し、artifact upload を整える
  - Docker smoke は make target まではあるが、CI 上の log 保存と runner 条件整理が未完
- [ ] runner 前提 (`qemu-system-i386`, Docker, 任意で `/dev/kvm`) を README/CI 設定へ落とす
  - 常用 CI に載せるには依存ツールと加速有無を明示する必要がある

2026-03-15 追記:

- `test-qemu-server` は ready marker 待ちと fault marker fail-fast を持つ常用 smoke に寄せた
- Docker/headless 側も同じ ready marker と fault marker を見る smoke を追加し、`SRT-08` の close path 修正後に常用 smoke へ移せる状態にした

### SRT-11: throttle 応答の retry 情報

- [ ] `admin_authorize_peer()` / `admin_authorize_request_detailed()` の `retry_after_ticks` を call site まで通す
- [ ] HTTP `429` に `Retry-After` と body 内の retry 値を載せる
- [ ] text protocol の `ERR throttled` に retry 値を載せる
- [ ] host test と smoke に throttle 応答の retry 検証を追加する

### SRT-12: 起動設定の fail-safe と可視化

- [ ] `/etc/sodex-admin.conf` の unknown key / parse error / size over を audit に残す
- [ ] token 欠落時にどこまで listener を立てるかを決め、ready marker に反映する
- [ ] config 不備時の fail-open / fail-closed を README と test で固定する
- [ ] `write_server_runtime_overlay.py` と Docker/QEMU smoke で設定不備ケースを再現できるようにする

### SRT-13: 異常系テストの拡充

- [ ] HTTP の `403` / `429` を QEMU/Docker smoke に入れる
- [ ] text protocol の `ERR forbidden` / `ERR unauthorized` / `ERR throttled` を回帰項目にする
- [ ] `busy` / `timeout` / `too_large` の host test を追加する
- [ ] `SODEX_ADMIN_ALLOW_IP` 上書きと config 差し替えの回帰を固定する

### SRT-14: allowlist の複数 peer / CIDR 対応

- [ ] runtime config で複数 `allow_ip` または CIDR を表現できる形式を決める
- [ ] `admin_is_source_allowed()` と ready marker の表現を複数 peer 前提へ広げる
- [ ] host test に複数 peer 許可 / 拒否ケースを追加する
- [ ] Linux host / Docker Desktop / CI runner を同一 build artifact で通せる smoke を用意する

### SRT-15: debug shell 用の最小 TCP-PTY bridge

2026-03-15 追記:

- raw TCP client 前提の `debug_shell_port` と `TOKEN <control_token>` preface を実装し、認証後は `OK shell` で `PTY` relay へ移る形にした
- `test_debug_shell_parser` で preface / role / config parser を固定した
- root `README` に `nc` / `socat` 前提の接続手順を追加した
- `test-qemu-debug-shell` と `run_qemu_debug_shell_smoke.py` を追加し、invalid preface / 認証失敗 / 認証成功 / reconnect まで QEMU smoke で通した

- [x] `telnet` 互換ではなく raw TCP client 前提の protocol と port を決める
- [x] listener を default off にし、明示 config がある時だけ有効にする
- [x] 接続直後の token preface と session upgrade を実装する
- [x] `openpty()` / `execve_pty()` で `eshell` を起動し、socket <-> `PTY` relay を実装する
- [x] 単一 session 制限、timeout、close、reconnect、audit を入れる
- [x] fixed winsize で始める方針に決める
- [x] host test を追加する
- [x] QEMU smoke を追加する
- [x] `nc` / `socat` 前提の接続手順を README に書く

### SRT-16: 暗号込み `SSH server` の段階導入

まずは [plans/08-ssh-server.md](plans/08-ssh-server.md) の最初の milestone、
すなわち `ssh -p <hostfwd_port> root@127.0.0.1` で `eshell` に入るところまでを最小 goal にする。

#### Phase 0: scope / profile 固定

- [x] listener を `ssh_port` default off にし、1 active connection / 1 session channel で固定する
- [x] 初期 scope を interactive shell のみに絞り、`exec` / `subsystem` / forwarding / 複数 channel を reject に固定する
- [x] 初期 auth を `password` のみに絞り、user 名は `root` 固定で進める
- [x] 初期 algorithm profile を `curve25519-sha256` + `ssh-ed25519` + `aes128-ctr` + `hmac-sha2-256` + `none` に固定する
- [x] `scp` / `sftp` / publickey auth / agent forwarding を初期スコープ外として文書に固定する

#### Phase 1: crypto / seed / config 注入

- [x] `src/lib/crypto/` を追加し、vendored primitive を最小 wrapper API で包む方針を決める
- [x] `X25519`, `Ed25519`, `SHA-256`, `SHA-512`, `AES-CTR`, `HMAC-SHA256` の最小 API を定義する
- [x] `ssh_password`, `ssh_hostkey_ed25519_seed`, `ssh_rng_seed` を config parser に追加する
- [x] seed 未設定時 fail closed にし、host 側 overlay 生成で seed/material を注入する
- [x] packet padding / ephemeral key / IV 用の DRBG を追加する

#### Phase 2: transport packet 層

- [x] `SSH-2.0-*` version exchange と banner parser を実装する
- [x] SSH binary packet の encode / decode を実装する
- [x] `uint32`, `string`, `name-list`, `mpint` helper を実装する
- [x] preauth state machine と disconnect reason を実装する
- [x] packet size 上限と malformed packet reject を入れる

#### Phase 3: key exchange / `NEWKEYS`

- [x] `KEXINIT` negotiation を 1 profile 固定で実装する
- [x] `curve25519-sha256` の shared secret と exchange hash を実装する
- [x] `ssh-ed25519` host key 公開鍵生成と署名を実装する
- [x] key expansion と `NEWKEYS` を実装する
- [x] `aes128-ctr` / `hmac-sha2-256` で encrypt/decrypt を有効化する

#### Phase 4: userauth

- [x] `ssh-userauth` service と `password` auth を実装する
- [x] `root` 固定 user + `ssh_password` 照合を実装する
- [ ] 認証失敗時の retry 制限を既存 runtime 制約へ統合する
- [x] timeout、audit、unsupported auth method の `password` advertise を既存 runtime 制約へ統合する

#### Phase 5: session / PTY / shell

- [x] `ssh-connection` service を実装する
- [x] `CHANNEL_OPEN session` を 1 本だけ許可する
- [x] `pty-req` を `tty_set_winsize()` と結び、初期 winsize と `window-change` を反映する
- [x] `shell` request で `openpty()` / `execve_pty()` に接続し、`eshell` を起動する
- [x] `CHANNEL_DATA` と `PTY` relay の双方向転送を実装する
- [x] shell 終了時に `exit-status`, `EOF`, `CLOSE` を返す
- [x] `exec`, `env`, `subsystem`, 追加 channel を reject する
- [ ] interactive shell を multi-command で安定化し、`pwd` に続けて `ls` / `exit` でも `shell_exit` せず prompt 復帰できるようにする
- [ ] host 側 `ssh -tt` の見え方を安定化し、prompt 未表示 run を潰す

#### Phase 6: host test / QEMU smoke / 手順

- [ ] host test に packet codec / `KEXINIT` parser / auth policy / channel state machine を追加する
- [ ] `run_qemu_ssh_smoke.py` を追加し、login / command / exit / reconnect / wrong password を通す
- [ ] host key fingerprint と known_hosts を固定する smoke 手順を追加する
- [ ] README に manual 手順と `ssh` / `ssh -tt` 例を書く

#### 現在の到達点

- [x] host の `ssh -tt -F /dev/null -o PreferredAuthentications=password -o PubkeyAuthentication=no -p <hostfwd_port> root@127.0.0.1` で guest `eshell` まで到達する
- [x] `sodex /> pwd` を実行して `/` が返るところまで確認する
- [ ] `OpenSSH` client の追加オプションなし login を通す
- [ ] `pwd` / `ls` / `exit` を同一 session で安定して通す
