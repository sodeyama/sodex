# Plan 05: 認証と Capability 境界

## 概要

server 面を作った時点で、認証なしの管理ポートを残すのは危険。
ここでは server 側から実行できる操作を制限し、接続元と権限を絞る。

## 目標

- token ベースの最小認証
- 接続元 allowlist
- role ごとの操作制限
- 操作ログの記録

## 設計

### 認証

- 起動時注入の static token から開始
- token がない場合、制御系 endpoint は起動しない

### 権限制御

- `health` role
- `status` role
- `control` role

のように役割を分ける。

### 接続制限

- private network 前提
- 同時接続数制限
- request size 上限
- idle timeout

## 実装ステップ

1. token の注入インターフェースを決める
2. 認証チェックを管理 protocol / HTTP に共通化する
3. handler ごとに required capability を定義する
4. audit log をシリアルまたは ring buffer に残す
5. rate limit と timeout を追加する

## 変更対象

- `src/net/admin_server.c`
- `src/net/http_server.c`
- `src/kernel.c`
- `src/include/*`
- `docs/ideas/docker-qemu-cloud-agent.md`

## 完了条件

- [ ] token なしで制御操作できない
- [ ] 接続元と権限を絞れる
- [ ] 失敗した認証と重要操作がログに残る
