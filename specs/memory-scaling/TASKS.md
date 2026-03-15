# Memory Scaling Tasks

`specs/memory-scaling/README.md` を、着手単位に落としたタスクリスト。
現状の固定メモリ前提を崩し、QEMU の `-m` と guest の allocator をつなぐ順で進める。

## 優先順

1. usable RAM 検出と layout policy
2. paging / allocator の動的化
3. QEMU memory 設定の一元化
4. matrix test と診断ログ

## M0: usable RAM 検出と layout policy

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | MS-01 | bootloader から kernel へ渡す `memory_info` / `memory_map` 構造を定義する | なし | raw address 読みではなく struct 経由でメモリ情報を取れる |
| [ ] | MS-02 | E820 を第一ソース、E801/88h を fallback とする usable RAM 解析 helper を実装する | MS-01 | kernel が usable range と総量を API で参照できる |
| [ ] | MS-03 | build/runtime cap を加味した `memory_layout_policy` を実装し、kernel heap / process pool の候補範囲を算出する | MS-02 | 固定 `32MB-64MB` ではなく policy から境界を計算できる |

## M1: paging / allocator の動的化

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | MS-04 | kernel direct map を 4MB PDE ベースで最大 1GB まで張れるようにする | MS-03 | `PGDIR_KERNEL_END` 相当が固定 64 PDE ではなく cap 由来になる |
| [ ] | MS-05 | `kalloc` / `aalloc` / `palloc` の free range を layout policy 由来へ置き換える | MS-04 | kernel heap と process pool が検出 RAM に応じて拡大する |
| [ ] | MS-06 | `execve` / `brk` / user process page 供給の回帰を追加し、大きい userland arena で破綻しないことを確認する | MS-05 | user process が高メモリ構成で従来通り起動し、`brk` も通る |

## M2: QEMU memory 設定の一元化

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | MS-07 | `bin/start.sh` と QEMU smoke script 群で使う共通 memory knob を定義する | MS-01 | `SODEX_QEMU_MEM_MB` など 1 系統の設定名で制御できる |
| [ ] | MS-08 | build 時既定値、runtime override、guest cap の優先順位を定義して make/start/test へ反映する | MS-07 | `128/256/512/1024MB` を手元で簡単に切り替えられる |

## M3: matrix test と診断ログ

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | MS-09 | host 側に memory layout policy の unit test を追加する | MS-03 | usable range、cap、fallback の計算を host test で固定できる |
| [ ] | MS-10 | QEMU 上で `128/256/512/1024MB` の boot / allocator smoke を追加する | MS-06, MS-08 | 高メモリ構成の回帰を QEMU で継続監視できる |
| [ ] | MS-11 | boot log と診断 API に「検出 RAM / 使用 cap / heap / process pool」を出す | MS-10 | メモリ不足や誤判定時に原因を画面と serial で追える |

## 先送りする項目

- 1GB 超の物理メモリ対応
- swap と page reclaim
- PAE / x86_64 化
- NUMA と memory hotplug
