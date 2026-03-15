# Sodex Agent を Linux + Docker + QEMU で動かす構想

## 目的

`sodex` を bare metal mini PC に先に載せるのではなく、まずは Linux ベースのマシン上で `Docker + QEMU` により安定して動かし、そこから `LLM API` と `MCP` に接続できる `Sodex Agent` を育てる。

## この方針を採る理由

- 現在の `sodex` は QEMU 上での開発と検証を前提にしている
- 既存のネットワーク実装は `NE2000 + uIP + QEMU` 前提で進んでいる
- bare metal 実機に先に合わせると、`UEFI`、`NIC`、`ストレージ` の差分が大きい
- `LLM API` や `MCP` を試す目的なら、まずは QEMU 上で外向き通信できることの方が重要

## 想定構成

```text
Linux ホスト
  └─ Docker
      └─ QEMU (headless)
          └─ sodex
              ├─ HTTP/TLS で LLM API に接続
              └─ HTTP 経由で MCP サーバに接続
```

## ホスト側の前提

- Linux ベースのマシン
- Docker が使える
- 可能なら `KVM` を使う
  - `QEMU` が `/dev/kvm` を使えると大幅に速くなる
  - `KVM` がない場合でも動作確認用には使えるが、性能はかなり落ちる

## QEMU 起動方針

### まず採るべき最短経路

- 現在の `build/bin/fsboot.bin` をそのまま QEMU に食わせる
- `-display none` と `-serial stdio` で headless に動かす
- ネットワークは当面 `NE2000` のまま維持する

この段階では `GRUB` や `Multiboot2` は不要。

理由:

- `GRUB + Multiboot2` は bare metal `UEFI` 実機向けの経路としては有力
- しかしクラウド上の QEMU で `sodex` を育てるだけなら、既存の `fsboot.bin` 経路の方が速い
- 先に `LLM API` と `MCP` の通信路を成立させるべき

### 将来の分岐

- bare metal 実機対応を再開する段階で `GRUB + Multiboot2 + kernel.elf` を追加検討する
- ただし cloud/Docker/QEMU 経路では、当面これは必須ではない

## ネットワーク方針

### 直近

- 既存の `NE2000` ドライバを使う
- QEMU の `ne2k_isa` で起動する
- `uIP` と `socket` の既存実装を足場にする

### 当面の目標

1. 固定IPで平文 TCP
2. 平文 HTTP
3. DNS
4. TLS
5. LLM API
6. MCP Client

## guest 管理面

2026-03-15 時点で、guest `sodex` には最小の管理面を追加した。

- host `127.0.0.1:18080` -> guest `10.0.2.15:8080`
- host `127.0.0.1:10023` -> guest `10.0.2.15:10023`
- `GET /healthz`, `GET /status`, `POST /agent/start`, `POST /agent/stop`
- text protocol の `PING`, `STATUS`, `AGENT START`, `AGENT STOP`, `LOG TAIL`

制御操作は `/etc/sodex-admin.conf` から起動時に読む token を必須にし、role と allowlist で絞る。
この段階では guest 側に `SSH server` は入れず、ホスト Linux の `SSH` と guest 管理 API を分離する。

## Sodex Agent の役割

`sodex` 自体は最小の agent runtime と通信層を持ち、重い処理や外部操作はなるべく外に逃がす。

### `sodex` 内で持つもの

- agent loop
- HTTP client
- TLS
- JSON パース
- MCP client
- 最小のタスク管理

### 外に逃がすもの

- ファイル操作の本体
- 長時間コマンド実行
- Git 操作
- 各種 SaaS 連携
- 大きいデータ処理

これらは MCP サーバ側に置く。

## MCP の想定

### 方式

- `sodex` から外部の MCP サーバへ HTTP で接続する
- まずは管理下の単純な MCP サーバだけを対象にする

### 典型構成

```text
sodex (guest)
  └─ MCP Client
      ├─ 外部 HTTP MCP サーバ
      └─ ホスト側のローカル管理用 MCP サーバ
```

### ホスト側 MCP サーバの用途

- Docker ホスト上のワークディレクトリ操作
- ジョブ実行
- ファイル読み書き
- Git 操作
- ログ収集

## 秘密情報の扱い

永続的に guest イメージへ焼き込まない。

候補:

- コンテナ環境変数
- マウントした設定ファイル
- 起動時注入

最初は単純にホスト側から注入し、後で短命トークン更新へ進める。

## 観測と運用

### ログ

- guest のシリアル出力をコンテナ標準出力へ流す
- API リクエスト/レスポンスの要点をログへ出す
- 失敗時は `QEMU`, `network`, `agent`, `MCP` を分けて見えるようにする

### 再起動

- まずはコンテナ再起動で十分
- 後で watchdog やヘルスチェックを追加する

## 現在の Docker 導線

2026-03-15 時点で、最小の headless Docker 導線を追加した。

- `docker/server-runtime/Dockerfile`
- `docker/server-runtime/entrypoint.sh`
- container 起動時に `/etc/sodex-admin.conf` 用 overlay を生成
- `make -C src SODEX_ROOTFS_OVERLAY=... all` で guest image を組み立て
- `bin/start.sh server-headless` で QEMU を foreground 実行

Linux で `/dev/kvm` を渡せる場合は `SODEX_QEMU_ACCEL=kvm` を使う。

## 性能の見立て

### 現実的に期待できること

- `KVM` が使えれば 1 台の `Sodex Agent` を動かす用途としては十分現実的
- `LLM API` や `MCP` はネット待ちが支配的なので、guest CPU の絶対性能はそこまで厳しくない

### 注意点

- `KVM` なしではかなり遅い
- 現状コードは busy-poll とデバッグ出力が多く、ここはボトルネックになりうる
- `TLS` を入れるとメモリと CPU の要求が上がる

## 実装方針

### Phase A: 実行基盤を作る

1. Dockerfile を作る
2. コンテナ内で `make` できるようにする
3. headless QEMU で `fsboot.bin` を起動できるようにする
4. ログを標準出力へ流す

### Phase B: 外向き通信を固める

1. `NE2000 + uIP` の現状を安定化
2. 平文 HTTP クライアント
3. DNS
4. TLS

### Phase C: agent 化する

1. LLM API クライアント
2. 応答パーサ
3. agent loop
4. MCP client

### Phase D: 運用可能性を上げる

1. ログ整理
2. シークレット注入
3. 再起動戦略
4. エラー分類

## この構想で明確に後回しにするもの

- bare metal mini PC への先行対応
- `GRUB + Multiboot2` による実機 `UEFI` 起動
- 実機 NIC ドライバ
- 実機ストレージドライバ

これらは cloud/QEMU 上で `Sodex Agent` の基本成立を確認した後に扱う。

## 成功条件

最低限の成功条件は以下。

1. Linux ホスト上の Docker コンテナで `sodex` が headless 起動する
2. `sodex` が外向きに HTTP/TLS 通信できる
3. `sodex` が LLM API を叩いて応答を受け取れる
4. `sodex` が MCP サーバに接続して限定された作業を実行できる

## 補足

この構想では、`sodex` を Linux 上のアプリとして動かすのではなく、`QEMU` 内のゲスト OS として動かす。Docker は `sodex` の実行環境そのものではなく、`QEMU` の配布・起動・監視をまとめる役割を持つ。
