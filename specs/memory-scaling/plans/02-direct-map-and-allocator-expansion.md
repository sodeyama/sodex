# Plan 02: kernel direct map と allocator の拡張

## 概要

Plan 01 で usable RAM と layout policy が取れるようになっても、
paging と allocator が固定 32MB/64MB のままでは高メモリ構成を使い切れない。

この plan では kernel direct map と allocator を policy 駆動へ移し、
現在の 3GB/1GB split の範囲で最大 1GB まで扱える基盤へ寄せる。

## 依存と出口

- 依存: 01
- この plan の出口
  - kernel direct map が cap に応じて広がる
  - `kalloc` / `aalloc` / `palloc` が固定アドレス帯ではなく policy 範囲を使う
  - `execve` / `brk` が高メモリ構成でも破綻しない

## 現状

- `create_kernel_page()` は 4MB PDE を 64 本しか張らず、kernel 仮想空間の 1GB を使い切っていない
- process page allocator は `32MB-64MB` の 32MB 固定
- `execve` / `brk` は allocator から page をもらう前提だが、供給元の headroom が小さい

## 方針

- kernel 仮想空間 `0xC0000000-0xFFFFFFFF` を direct map 上限とする
- process pool はまず「最大の連続 usable span」を採る
- low memory の予約領域と device hole を侵食しない
- 初手では allocator の API を大きく変えず、backing range だけ動的化する

## 設計判断

- 1GB direct map は 4MB PDE 256 本で構成する
- kernel heap と process pool は同じ policy から切り出す
- 多 zone allocator は後回しにし、まず contiguous span 前提で低リスクに進める
- `MAX_MHOLES` / `MAX_PMHOLES` は必要に応じて見直すが、plan の主目的は range の拡張に置く

## 実装ステップ

1. `page.c` の kernel direct map 上限を policy 参照へ置き換える
2. kernel heap 初期 free range を policy から決める
3. process pool 初期 free range を policy から決める
4. `execve` / `set_process_page` / `sys_brk` で高メモリ構成を検証する
5. allocator の統計と失敗ログを増やし、どこで足りないか追えるようにする

## 変更対象

- 既存
  - `src/include/page.h`
  - `src/page.c`
  - `src/include/memory.h`
  - `src/memory.c`
  - `src/execve.c`
  - `src/brk.c`
- 新規候補
  - `tests/test_memory_layout.c`
  - `src/test/run_qemu_memory_smoke.py`

## 検証

- `128MB` 構成で現行 boot が維持される
- `512MB` / `1024MB` 構成で `palloc` と `brk` の headroom が増える
- user process が起動し、`malloc` と file I/O が従来通り動く

## 完了条件

- QEMU が多めの RAM を持つとき、guest も allocator レベルでそれを使える
- 大規模辞書や cache のための headroom が確保できる
