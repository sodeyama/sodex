# Plan 1.9: QEMU 固定値の整理

## 目的

NE2000 bring-up が `QEMU 固定` の実装になっている箇所を減らし、ドライバとテスト設定の責務を分ける。

## 現状

直書きが残っている。

- `src/net/netmain.c` の ISR 読み出しで `0xC100`
- `src/kernel.c` の NE2000 初期化後デバッグで `0xC100`
- `src/test/ktest.c` のテスト初期化で `0xC100`
- `src/drivers/ne2000.c` の `get_baseaddr()` が `NE2K_QEMU_BASEADDR` 固定

## タスク

1. I/O base 参照を 1 箇所に寄せる
2. `kernel.c` / `ktest.c` から NIC レジスタ直叩きを減らす
3. QEMU 起動引数とドライバ設定の対応関係を spec に明記する

## 方針

- ドライバ内部では `io_base` を正として扱う
- 外部コードは可能な限り `io_base` か accessor を使う
- test 固有の値は test 側設定として閉じ込める

## 完了条件

- [ ] `0xC100` の重複参照が整理されている
- [ ] NIC ベースアドレスの変更点が追いやすい
- [ ] QEMU 前提値が driver / test / doc のどこに属するか明確
