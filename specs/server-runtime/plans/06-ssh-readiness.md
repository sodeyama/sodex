# Plan 06: SSH 対応可否の評価

## 概要

`sodex` 自身に `SSH server` を入れる価値があるかを判断するための整理。
この plan は「すぐ実装する」ためではなく、前提とコストを見積もるためのもの。

## なぜ後回しか

- `accept()` すら未完成の段階で `SSH` はスコープが広すぎる
- `SSH` には TCP server 以外に、鍵交換、暗号、MAC、認証、PTY、セッション制御が必要
- cloud/Docker/QEMU 構成では、ホスト Linux 側の `SSH` を使うだけでかなり運用できる

## `SSH` 実装に必要な要素

1. 安定した passive TCP
2. PTY と shell 起動の server 側統合
3. 乱数源
4. 鍵交換
5. 暗号化 / MAC
6. host key の注入または永続化
7. ユーザー認証
8. 接続・再接続・切断の管理

## 比較対象

### 直接 `sshd` を作る場合

- 利点
  - 一般的な client から接続できる
- 欠点
  - 実装量が大きい
  - 攻撃面が広い

### 代替案

- ホスト Linux に `SSH`
- guest `sodex` には軽量管理 protocol / HTTP

この構成の利点:

- いま必要な機能を先に得られる
- guest 側の attack surface が小さい

## 判定基準

以下を満たして初めて `SSH` 実装を検討する。

- passive TCP が安定
- 管理 API と認証が完成
- TLS/暗号基盤がある程度揃っている
- PTY 制御を server 側から安全に扱える

## 出口

- `SSH` をやる
- `SSH` はやらず、ホスト Linux + guest 管理 API で十分とする

のどちらかを明示的に決める。

## 現時点の判断

2026-03-15 時点では `SSH` 実装は `no-go` とする。

- host Linux 側の `SSH` と guest `sodex` の管理 API 分離で運用要件を満たせる
- guest 側は text protocol / HTTP / token / allowlist / audit に絞った方が attack surface を小さく保てる
- `TLS`, `PTY`, 鍵管理, 認証方式の整理が終わるまでは `SSH` を追加しない

## 完了条件

- [x] `SSH` の前提条件が整理されている
- [x] 代替案との比較ができている
- [x] go/no-go を判断できる材料が揃っている
