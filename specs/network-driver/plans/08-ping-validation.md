# Plan 1.8: ping 疎通の完了条件を閉じる

## 目的

`specs/network-driver` のゴールである「QEMU 上で ping が通る」を、曖昧な確認ではなく再現可能な完了条件にする。

## 完了メモ

この plan は完了。

- `src/test/ktest.c` に `icmp_ping_gateway` を追加
- `kern_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)` で raw ICMP を送信
- QEMU `user net` の gateway `10.0.2.2` から echo reply を受信
- `make test-qemu` で自動確認できる

## 現状

- `src/usr/command/ping.c` は存在する
- `src/test/run_qemu_ktest.py` は `guestfwd` を使った TCP echo 検証まで入っている
- `07-qemu-test.md` は `ping` をゴールとしているが、自動検証までは落ちていない
- `user net` ではホストからゲストへ直接 `ping` しづらい

## タスク

1. `ping` の検証方式を 1 つに決める
2. その方式に合わせてテスト手順を固定する
3. spec の完了条件を更新する

## 候補

### A. ゲスト内 `ping 10.0.2.2`

長所:

- QEMU `user net` のままでよい
- クラウド上の headless 構成に近い

短所:

- テスト起動後にユーザ空間コマンドをどう実行するか整理が必要

### B. tap / bridge でホストから `ping`

長所:

- 観測がわかりやすい

短所:

- クラウドや Docker では再現性が落ちる
- セットアップ依存が増える

## 採用した方式

- QEMU `user net`
- `ktest` から `10.0.2.2` へ raw ICMP echo を送信
- `icmp_ping_gateway` の pass を完了条件にする

## 完了条件

- [x] `ping` の正規テスト方式が 1 つに定義されている
- [x] その方式で成功確認できる
- [x] `README.md` と `07-qemu-test.md` の完了条件が一致している
