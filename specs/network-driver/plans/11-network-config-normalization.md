# Plan 1.11: ネットワーク設定の正規化

## 目的

通常起動、QEMU test、将来の headless 運用でネットワーク設定の意味を揃える。

## 現状

設定が揺れている。

- `src/kernel.c` では gateway が `10.0.2.1`
- `src/test/ktest.c` では gateway が `10.0.2.2`
- `guestfwd` 用の接続先は `10.0.2.100`

## タスク

1. `user net` 前提の標準値を定義する
2. 通常起動と test 起動の差分を明文化する
3. spec とコードの値を一致させる

## 方針

- 標準値を 1 組決める
- test 専用の差分は理由つきで残す
- 可能なら設定定義を共有化する

## 完了条件

- [ ] host IP / gateway / guest IP の意味が文書化されている
- [ ] `kernel.c` と `ktest.c` の差分理由が説明できる
- [ ] spec のネットワーク例が現行実装と一致する
