# Server Runtime Spec

`sodex` を `Linux ホスト + Docker + QEMU` 上で常時起動し、外向きの Agent としてだけでなく、限定的な server としても使えるようにするための計画。

## 背景

現在の `sodex` は client 側の通信経路を優先して実装しており、`connect()` / `send()` / `recv()` はある程度形になっている。
一方で server 側は `bind()` / `listen()` までは存在するが、`accept()` が未実装で、受け口としてはまだ成立していない。

また、`SSH server` を直接目標にすると、TCP 受け付け、認証、暗号、PTY 統合まで一気に広がってしまう。
そのため、この spec ではまず以下の順で段階導入する。

1. 受動 TCP 接続の成立
2. 軽量な管理プロトコルまたは HTTP 制御面
3. 認証と権限制御
4. 将来の `SSH` 対応可否の判断

## ゴール

- QEMU guest の `sodex` が TCP server として接続を受けられる
- cloud 上の Docker/QEMU 構成で、ホストから guest へ制御用接続を転送できる
- `sodex` に health / status / agent control のための最小の server 面を持たせる
- server 側の操作を `deny-by-default` で制限し、Agent/MCP 実行権限と分離できる
- `SSH` を本当に実装すべきか、代替案で十分かを判断できる

## 非ゴール

- 最初から public internet に直接さらす汎用 server にしない
- いきなり `sshd` 互換を作らない
- `scp`, `sftp`, 多ユーザー管理, PAM 相当の仕組みは対象外
- 実機 NIC や bare metal `UEFI` 対応はこの spec の主題にしない

## 想定デプロイ

```text
Linux ホスト
  ├─ sshd
  └─ Docker
      └─ QEMU
          └─ sodex
              ├─ outbound: LLM API / MCP
              └─ inbound: 管理用 TCP / HTTP
```

最初の remote access はホスト Linux の `SSH` を使い、guest `sodex` へは QEMU のポートフォワード越しに管理用接続を入れる。
`sodex` 自身に `SSH` を入れるのは後段の検討事項とする。

## 現状整理

### あるもの

- `AF_INET` の socket API 骨格
- `bind()` / `listen()`
- `connect()` / `send()` / `recv()`
- `uIP` ベースの TCP / UDP 処理
- `network_poll()` によるポーリング駆動

### 足りないもの

- `accept()`
- 受信側 TCP 接続を socket に割り当てる仕組み
- backlog 管理
- 管理プロトコル
- 認証
- 監査ログ
- `SSH` に必要な暗号・鍵交換・セッション管理

## フェーズ

| # | ファイル | 概要 | 依存 |
|---|---------|------|------|
| 01 | [01-passive-tcp-foundation.md](plans/01-passive-tcp-foundation.md) | `accept()` を含む受動 TCP の基盤 | `specs/network-driver/` |
| 02 | [02-qemu-ingress-and-service-loop.md](plans/02-qemu-ingress-and-service-loop.md) | Docker/QEMU 上の受信経路と常駐サービスループ | 01 |
| 03 | [03-admin-protocol.md](plans/03-admin-protocol.md) | 軽量な管理用 TCP プロトコル | 01, 02 |
| 04 | [04-http-control-surface.md](plans/04-http-control-surface.md) | HTTP ベースの control / health 面 | 01, 02 |
| 05 | [05-auth-and-capability-boundary.md](plans/05-auth-and-capability-boundary.md) | 認証、権限制御、接続制限 | 03 または 04 |
| 06 | [06-ssh-readiness.md](plans/06-ssh-readiness.md) | `SSH` 実装の前提と go/no-go 判定 | 01-05 |
| 07 | [07-tcp-pty-bridge.md](plans/07-tcp-pty-bridge.md) | 暗号なしで shell relay を検証する最小 TCP-PTY bridge | 01-06 |
| 08 | [08-ssh-server.md](plans/08-ssh-server.md) | 暗号込み `SSH server` の段階導入 | 06, 07 |

実装タスクと残フォローアップは [TASKS.md](TASKS.md) で管理する。

## 実装順序

1. `accept()` と passive open を完成させる
2. QEMU `hostfwd` 経由で guest の待受ポートへ届く状態を作る
3. shell を露出しない軽量管理プロトコルを先に入れる
4. health / status / 制御操作を HTTP 化するか判断する
5. token / capability / allowlist で絞る
6. その上で `SSH` が必要かを再評価する
7. 必要なら最小 TCP-PTY bridge で shell relay を先に検証する
8. それでも必要なら暗号込み `SSH server` を 1 suite から段階導入する

## 設計判断

- 最初の server 面は private な管理ネットワーク前提にする
- remote shell をいきなり作らず、`named action` と `status read` を優先する
- `SSH` は「必要ならやる」であって初期スコープには入れない
- cloud 運用ではホスト Linux の `SSH` と guest `sodex` の管理 API を分離する

## 変更候補

- 既存
  - `src/socket.c`
  - `src/net/uip-conf.c`
  - `src/net/netmain.c`
  - `src/kernel.c`
  - `src/usr/include/sys/socket.h`
  - `src/include/socket.h`
- 新規候補
  - `src/usr/command/srvctl.c`
  - `src/net/admin_server.c`
  - `src/net/http_server.c`
  - `src/include/admin_server.h`
  - `src/test/run_qemu_server_smoke.py`
  - `tests/test_socket_server.c`

## 実装状況

2026-03-15 時点で、以下を確認済み。

- passive TCP の inbound accept と backlog 処理
- `QEMU user net + hostfwd` による host `127.0.0.1:18080` -> guest `10.0.2.15:8080`
- `QEMU user net + hostfwd` による host `127.0.0.1:10023` -> guest `10.0.2.15:10023`
- text protocol の `PING`, `STATUS`, `AGENT START`, `AGENT STOP`, `LOG TAIL`
- HTTP の `GET /healthz`, `GET /status`, `POST /agent/start`, `POST /agent/stop`
- `/etc/sodex-admin.conf` による起動時 token / allowlist 注入
- allowlist と audit ring buffer
- 認証失敗に対する peer 単位 rate limit と backoff
- `docker/server-runtime/Dockerfile` / `entrypoint.sh` / `run_docker_server_smoke.py` による Docker/headless 常駐起動と published-port smoke
- `debug_shell_port` と raw TCP preface、`PTY` relay、`test-qemu-debug-shell` による reconnect smoke
- host Linux の `SSH` と guest `sodex` 管理 API の分離を継続し、guest 内 `SSH server` は当面見送る判断

## 完了条件

- [x] guest `sodex` が TCP 接続を受けられる
- [x] Docker/QEMU 上で host から guest 管理ポートへ接続できる
- [x] 最低限の health / status / control 操作ができる
- [x] 認証なしの広い制御面を残さない
- [x] `SSH` を実装するか、代替構成で十分とするかを文書で決められる
