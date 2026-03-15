# Memory Scaling Spec

QEMU に与えた RAM を guest が固定 64MB 前提ではなく実際に使えるようにし、
将来的に 512MB から 1GB 級の辞書や cache を扱える土台を作るための計画。

狙いは「QEMU の `-m` は増やしたが、guest の allocator と paging は 32MB/64MB 固定のまま」
という状態をやめ、boot 時に検出した物理メモリ量を kernel が layout policy に従って
自動的に利用する構成へ移すこと。

## 背景

現状の制約:

- `src/include/memory.h` の `KERNEL_PMEMBASE` / `KERNEL_PMEMEND` が `32MB-64MB` 固定
- `src/page.c` の kernel direct map は `PGDIR_KERNEL_START+64` までで、4MB PDE 換算で `256MB` しか張っていない
- `bin/start.sh` と各種 QEMU smoke script は `-m 128` を個別に直書きしている
- `bootmiddle.S` は BIOS からメモリサイズや E820 map を取っているが、allocator policy へ十分つながっていない
- userland 側の `malloc` / `brk` は process page 供給量に強く依存し、大きい辞書をそのまま載せにくい

この構造では以下が難しい:

- QEMU 起動時に `-m 512` や `-m 1024` を渡しても guest が自動で恩恵を受けること
- build 時の既定 RAM と runtime override の両立
- 512MB 以上の RAM を使う前提の file cache や大規模 IME 辞書
- 低メモリ構成と高メモリ構成の両方を同じコード経路で検証すること

## ゴール

- guest が boot 時に検出した usable RAM をもとに kernel heap / process pool を動的に決められる
- QEMU の `-m` を `128MB`, `256MB`, `512MB`, `1024MB` で変えても guest が自動で追従できる
- build 時の既定値と runtime override の両方を持てる
- 現在の 3GB/1GB split のまま、最大 1GB までの kernel direct map を扱える
- host test と QEMU matrix で低メモリ/高メモリの回帰を継続監視できる

## 非ゴール

- PAE や x86_64 化による 1GB 超の物理メモリ対応
- swap や demand paging
- NUMA、hotplug memory、ballooning
- 初手からの多 zone allocator 化や page reclaim

## 目標アーキテクチャ

```
          ┌──────────────────────────────────────┐
          │ QEMU                                │
          │  - -m 128 / 256 / 512 / 1024        │
          └────────────────┬─────────────────────┘
                           │ BIOS E820 / E801 / 88h
          ┌────────────────┴─────────────────────┐
          │ bootmiddle.S                         │
          │  - raw memory size                   │
          │  - usable memory map                 │
          └────────────────┬─────────────────────┘
                           │ boot info
          ┌────────────────┴─────────────────────┐
          │ kernel memory policy                  │
          │  - cap (build/runtime)                │
          │  - reserved ranges                    │
          │  - kernel heap range                  │
          │  - process page pool range            │
          └────────────────┬─────────────────────┘
                           │
          ┌────────────────┴─────────────────────┐
          │ paging / allocators                   │
          │  - kernel direct map up to 1GB        │
          │  - kalloc / palloc / brk              │
          └────────────────┬─────────────────────┘
                           │
          ┌────────────────┴─────────────────────┐
          │ userland                              │
          │  - larger malloc/brk headroom         │
          │  - file cache / IME dictionary        │
          └──────────────────────────────────────┘
```

## 実装ポリシー

- まず「どこまで使える RAM か」を API 化する
  - 生の magic address 読みではなく、kernel が参照する boot memory info を定義する
- layout policy を 1 箇所に集める
  - direct map 範囲、kernel heap、process pool を定数散在で決めない
- 1GB を上限とする
  - 現在の `__PAGE_OFFSET=0xC0000000` では kernel 仮想空間は 1GB なので、そこを上限にする
- 低メモリ fallback を残す
  - E820 が壊れていても `64MB` 既定で boot できる経路は保持する
- QEMU 引数は 1 箇所の設定に寄せる
  - `bin/start.sh` と各 Python smoke script に `-m 128` を散らさない

## Plans

| # | ファイル | 概要 | 依存 | この plan の出口 |
|---|---------|------|------|-------------------|
| 01 | [01-memory-discovery-and-layout.md](plans/01-memory-discovery-and-layout.md) | BIOS/E820 の結果を boot info と layout policy へつなぐ | なし | kernel が「使える RAM」と「使う RAM cap」を API として参照できる |
| 02 | [02-direct-map-and-allocator-expansion.md](plans/02-direct-map-and-allocator-expansion.md) | paging と allocator を固定 32MB/64MB 前提から動的構成へ移す | 01 | kernel direct map と process page allocator が 1GB まで拡張可能になる |
| 03 | [03-qemu-memory-configuration.md](plans/03-qemu-memory-configuration.md) | build 時既定値と runtime QEMU 引数を統一して切り替え可能にする | 01 | `bin/start.sh` と QEMU smoke 全体で同じ memory knob を使える |
| 04 | [04-validation-and-compatibility.md](plans/04-validation-and-compatibility.md) | host/QEMU の検証、fallback、診断ログを固める | 02, 03 | 低メモリ/高メモリの matrix が回り、壊れ方が追いやすくなる |

## マイルストーン

| マイルストーン | 対象 plan | 到達状態 |
|---------------|-----------|----------|
| M0: usable RAM の見える化 | 01 | boot 時に検出したメモリ量と usable range を kernel が構造化して参照できる |
| M1: allocator の動的化 | 02 | kernel heap / process pool が fixed constant ではなく policy 由来になる |
| M2: QEMU RAM 設定の一元化 | 03 | build 既定値と runtime override が `start.sh` と smoke test で共通化される |
| M3: 高メモリ構成の継続検証 | 04 | `128/256/512/1024MB` の検証経路と診断ログが揃う |

## 現在の到達点

- BIOS 由来のメモリサイズ取得は bootloader に存在する
- kernel direct map は 4MB PDE を 64 本だけ張っており、実質 `256MB` までしか見ない
- process physical allocator は `32MB-64MB` の 32MB 固定範囲だけを使う
- `bin/start.sh` と QEMU smoke scripts は `-m 128` を個別に直書きしている
- 1GB 構成を前提にした boot / allocator / userland の継続検証はまだ無い

## 実装順序

1. まず boot 時 memory info と layout policy を分離し、kernel が usable RAM を構造化して読めるようにする
2. 次に paging と allocator を fixed constant から policy 駆動へ置き換える
3. その上で QEMU の `-m` を build/runtime から一元設定できるようにする
4. 最後に低メモリと高メモリの matrix test、診断ログ、fallback を固める

## 並行化の考え方

- `01` の boot info struct と host test は先行できる
- `02` の paging と allocator は密結合なので同じ流れで進める
- `03` の QEMU 引数統一は `01` の naming が固まれば並行で進められる
- `04` は `02`, `03` の両方が出揃ってから matrix 化する

## 主な設計判断

- 1GB は「現在の 3GB/1GB split で扱える最大値」として扱う
  - `0xC0000000-0xFFFFFFFF` の kernel 仮想空間へ、物理 `0-1GB` を 4MB PDE で直写し切る
- usable RAM は E820 を第一ソースにする
  - E801 / 88h は fallback size として扱い、reserved range の精度は E820 を優先する
- guest が使う RAM 上限は 2 段階にする
  - QEMU `-m` による実 RAM
  - build 時 cap または kernel runtime cap による論理上限
- process pool は最初は「最大の連続 usable span」を採る
  - 初手から多 zone allocator へ広げず、QEMU の素直な E820 配置を前提に低リスクで進める
- 低メモリ構成は引き続き第一級で扱う
  - 1GB に寄せるが、128MB でも boot/test できることを壊さない

## 未解決論点

- kernel heap と process pool の境界を固定比率にするか、最小保証 + 残り配分にするか
- `MAX_PMHOLES` / `MAX_MHOLES` を固定配列のまま拡張するか、将来的に動的管理へ寄せるか
- 1GB 既定を開発者向け default にするか、CI/日常は 128MB 維持にするか
- file cache や大規模 IME 辞書に対して kernel/user のどちらへ budget を寄せるか
