# Plan 01: usable RAM 検出と layout policy

## 概要

QEMU に `-m 512` や `-m 1024` を渡しても、現在の sodex は固定定数が多く、
boot 時に見えた RAM を allocator と paging の policy に十分つなげていない。

この plan では BIOS/E820 由来のメモリ情報を boot info として整理し、
kernel が「何 MB 見えたか」ではなく「どの物理範囲をどこまで使うか」を
構造化して判断できる土台を作る。

## 依存と出口

- 依存: なし
- この plan の出口
  - kernel が usable RAM 総量と usable range を struct API で参照できる
  - E820 / E801 / 88h の優先順位と fallback が定義される
  - build/runtime cap を反映した `memory_layout_policy` が計算できる

## 現状

- `bootmiddle.S` は E820 とメモリサイズを取っている
- しかし kernel 側では `KERNEL_PMEMBASE` / `KERNEL_PMEMEND` の固定値依存が強い
- `MAXMEMSIZE` の生読みは存在するが、usable range と reserved range の区別が API 化されていない

## 方針

- E820 を第一ソースにする
  - usable / reserved を range として扱う
- E801 / 88h は fallback size とする
  - E820 が使えない場合だけ total size 推定に使う
- `memory_info` と `memory_layout_policy` を分ける
  - 前者は boot 由来の事実
  - 後者は kernel が採用する運用方針

## 設計判断

- `memory_info` には少なくとも以下を持たせる
  - 総 RAM
  - usable range 配列
  - reserved range 配列
  - 情報源種別
- layout policy は以下を返す
  - 使用 RAM cap
  - kernel direct map 上限
  - kernel heap range
  - process pool range
- cap は `min(detected_ram, configured_cap)` とする
- E820 が欠けても `64MB` fallback boot は残す

## 実装ステップ

1. bootloader と kernel の間で共有する `memory_info` struct を定義する
2. E820 / E801 / 88h の結果を kernel 側へ受け渡す
3. usable / reserved range を正規化する helper を作る
4. configured cap を食わせて layout policy を返す helper を作る
5. serial / screen へ検出結果を出す最小ログを追加する

## 変更対象

- 既存
  - `src/bootmiddle.S`
  - `src/include/memory.h`
  - `src/memory.c`
- 新規候補
  - `src/include/memory_layout.h`
  - `src/memory_layout.c`
  - `tests/test_memory_layout.c`

## 検証

- E820 がある構成で usable range が正しく読める
- E820 が欠ける場合に E801 / 88h fallback が働く
- `128MB`, `512MB`, `1024MB` 相当の layout policy が期待通りの cap を返す

## 完了条件

- kernel が fixed constant ではなく boot info + policy をもとにメモリ境界を決められる
- 次の paging / allocator plan がこの API に乗って進められる
