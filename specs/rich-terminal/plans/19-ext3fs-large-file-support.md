# Plan 19: ext3fs large file support

## 目的

Plan 18 の大規模辞書 blob を実用語彙まで拡張すると、単一 file が数 MiB 級になる。
現状の `mkfs` と runtime `ext3fs` は single indirect 前提が強く、dictionary blob を
image へ安定して載せ続ける土台としては不足している。

この plan では `kmkfs` と runtime `ext3fs` を double indirect まで対応させ、
multi-MiB 級の単一 file を build 時に生成し、guest から読める状態にする。

## スコープ

- `kmkfs` が direct + single indirect + double indirect を生成できる
- runtime `ext3fs` が large file の read / write / free を double indirect まで扱える
- file 上限と block geometry を header / tool 間で揃える
- host test と QEMU smoke で 5MiB 級 fixture の読み検証を固定する

## 非ゴール

- triple indirect 対応
- extents や journaling の導入
- 複数 block group や inode allocator 全体の作り直し
- Plan 18 の辞書 source / license 決定そのもの

## 実装項目

1. ext3 block geometry の共通化
   - `BLOCK_MAX`、direct 数、pointer per block、stage 境界を header へ寄せる
   - tool / kernel / userland で同じ上限を使う
2. `kmkfs` の large file image 生成
   - direct、single indirect、double indirect の block 割り当てを実装する
   - 間接 block 自体も bitmap へ反映する
   - partial block は zero pad して image 内容を安定化する
3. runtime `ext3fs` の double indirect 対応
   - `get_block`、`ensure_block`、`release_inode_blocks` を拡張する
   - single indirect の既存経路を壊さない
4. guest 側検証導線
   - large file の境界 block を読む `fslarge` command を追加する
   - 代表 block の prefix と size を report file に書き出す
5. 回帰テスト
   - host test で block position と `BLOCK_MAX` 境界を固定する
   - QEMU smoke で 5MiB fixture を image に載せ、guest から verify する

## 検証

- `make -C tests test_ext3fs && ./tests/test_ext3fs`
- `make -C src all`
- `make -C src test-qemu-fs`
- `make -C src test-qemu-ext3-large`

## この plan の出口

- `/usr/bin` に 5MiB 級 file を image build で載せられる
- guest userland から double indirect 領域を含む block を読める
- large dictionary blob を fs 制約で諦めずに進められる
