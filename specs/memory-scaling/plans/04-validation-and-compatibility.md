# Plan 04: 高メモリ構成の検証と互換性固定

## 概要

memory scaling は boot、paging、allocator、QEMU wrapper を横断して壊れやすい。
実装だけ先に進めると、128MB では動くが 1GB で壊れる、またはその逆が起きやすい。

この plan では host/QEMU の検証と診断ログを固め、
低メモリと高メモリの両方を継続監視できる状態へ持っていく。

## 依存と出口

- 依存: 02, 03
- この plan の出口
  - `128/256/512/1024MB` の matrix が回る
  - boot log にメモリ検出結果と policy が出る
  - 失敗時に原因を追える診断情報が残る

## 現状

- memory scaling 専用の host/QEMU matrix は無い
- boot 時に「何 MB 見えて何 MB 使うか」の情報が十分に出ていない
- smoke test は主に terminal 機能中心で、メモリ量の違いを検証していない

## 方針

- pure logic は host test、paging/allocator は QEMU で見る
- matrix はまず `128/256/512/1024MB` の 4 点に絞る
- 高メモリ構成だけでなく低メモリ fallback も固定する

## 設計判断

- boot log には最低限以下を出す
  - detected RAM
  - effective cap
  - kernel heap range
  - process pool range
- QEMU matrix は専用 smoke script または `make` target で回せるようにする
- failure は screen だけでなく serial / qemu log から追えるようにする

## 実装ステップ

1. host 側に layout policy test を追加する
2. QEMU 上で boot / allocator / user process を見る memory smoke を追加する
3. `128/256/512/1024MB` の matrix target を追加する
4. boot log と failure message を整理する
5. README に検証手順を追記する

## 変更対象

- 既存
  - `tests/Makefile`
  - `src/makefile`
  - `README.md`
- 新規候補
  - `tests/test_memory_layout.c`
  - `src/test/run_qemu_memory_smoke.py`

## 検証

- host test で layout policy の境界条件が通る
- `128MB` と `1024MB` の両方で boot できる
- 大きめの `brk` / `malloc` / IME 辞書 load を想定した smoke が通る

## 完了条件

- memory scaling が一時的な bring-up ではなく、継続的に守られる
- 後続の大規模 IME 辞書 plan がこの検証基盤を前提に進められる
