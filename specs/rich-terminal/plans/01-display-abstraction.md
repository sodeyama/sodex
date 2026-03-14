# Plan 01: 表示 backend 抽象化

## 概要

現在の `_kputc()` / `_kprintf()` は VGA テキスト VRAM に直接書き込んでいる。
これを `display backend` 越しに出力する構造へ変え、将来の framebuffer backend を差し込めるようにする。

## 現状

- `src/vga.c` が `screenX`, `screenY`, `promptX`, `promptY` を内部保持
- `_poscolor_printc()` は `src/sys_core.S` で `80x25` 前提計算をしている
- `SCREEN_WIDTH` / `SCREEN_HEIGHT` が `src/include/vga.h` に固定値で定義されている

## 方針

- まず console API と backend API を分離する
- 初期段階では backend として「既存 VGA text backend」を実装し互換維持する
- 文字セル座標、色、カーソル更新、スクロールを backend 側へ閉じ込める

## 実装ステップ

1. `struct display_backend` を導入し、`put_cell`, `scroll`, `clear`, `set_cursor`, `flush` を持たせる
2. 既存 `vga.c` のロジックを `vga_text_backend.c` 相当へ分離する
3. `_kputc()` は直接 VRAM ではなく「現在の console backend」に委譲する
4. `SCREEN_WIDTH/HEIGHT` の固定値依存を backend の `cols/rows` 参照へ置き換える
5. `src/sys_core.S` の `80` 定数使用箇所を C 側から与える設計へ寄せる

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

## 完了条件

- VGA テキスト backend で現行表示が壊れない
- 80x25 固定値が console API から見えなくなる
- framebuffer backend を後から差し込める

