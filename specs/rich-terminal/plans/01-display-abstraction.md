# Plan 01: 表示 backend 抽象化

## 概要

現在の `_kputc()` / `_kprintf()` は VGA テキスト VRAM に直接書き込んでいる。
これを `display backend` 越しに出力する構造へ変え、将来の framebuffer backend を差し込めるようにする。

## 依存と出口

- 依存なし
- この plan の出口
  - `_kputc()` / `_kprintf()` が backend 経由の互換 API になる
  - console 利用側から `80x25` 固定が見えなくなる
  - 後続 plan が VGA text と framebuffer を同じ抽象で扱える

## 現状

- `src/vga.c` が `screenX`, `screenY`, `promptX`, `promptY` を内部保持
- `_poscolor_printc()` は `src/sys_core.S` で `80x25` 前提計算をしている
- `SCREEN_WIDTH` / `SCREEN_HEIGHT` が `src/include/vga.h` に固定値で定義されている

## 方針

- まず console API と backend API を分離する
- 初期段階では backend として「既存 VGA text backend」を実装し互換維持する
- 文字セル座標、色、カーソル更新、スクロールを backend 側へ閉じ込める

## 設計判断

- `_kputc()` / `_kprintf()` の public API は当面残す
  - 呼び出し側の広範な変更を避け、内部だけ backend 化する
- backend が `cols`, `rows`, `cursor`, `scroll` を責務として持つ
  - console 利用側は「何文字書くか」だけを意識する
- assembly 側の固定値使用は C helper へ寄せる
  - `sys_core.S` に terminal 幅の知識を残さない
- flush は no-op を許容する
  - VGA text backend は即時描画、framebuffer backend は明示 flush でもよい

## 実装ステップ

1. `struct display_backend` と `struct console_state` を定義する
2. `vga.c` のカーソル位置、色、スクロール処理を VGA text backend 実装へ分離する
3. `_kputc()` / `_kprintf()` / 画面クリア経路を `console_write()` 相当へ集約する
4. `SCREEN_WIDTH/HEIGHT` 依存を backend の `cols/rows` 参照へ置き換える
5. `src/sys_core.S` の 80 桁前提を C helper 呼び出しまたは共有変数参照へ寄せる
6. graphics backend 未導入時でも boot log が壊れないことを確認する
7. `tests/test_vga.c` を拡張し、書式化とスクロールの回帰を確認できるようにする

## 変更対象

- 既存
  - `src/vga.c`
  - `src/include/vga.h`
  - `src/sys_core.S`
- 新規候補
  - `src/display/console.c`
  - `src/include/display/console.h`
  - `src/display/vga_text_backend.c`
  - `src/include/display_backend.h`
  - `tests/test_vga.c`

## 検証

- 現在の boot log と panic 出力が崩れない
- `tests/test_vga` が通る
- 後続で backend を差し替えても `_kputc()` 呼び出し側に変更が広がらない

## 完了条件

- VGA テキスト backend で現行表示が壊れない
- 80x25 固定値が console API から見えなくなる
- framebuffer backend を後から差し込める
