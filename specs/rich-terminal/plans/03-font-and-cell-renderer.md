# Plan 03: フォントと文字セルレンダラ

## 概要

framebuffer の上に terminal を作るため、ビットマップフォントと文字セル単位の描画器を作る。
ここで初めて「80桁固定ではない terminal」が成立する。

## 依存と出口

- 依存: 02
- この plan の出口
  - `term_cell` / `terminal_surface` / glyph 描画が揃う
  - 解像度から `cols`, `rows` を求められる
  - surface 更新を host test できる形になる

## 方針

- 初期実装は ASCII 中心の固定幅ビットマップフォント
- `cols = width / cell_width`, `rows = height / cell_height` で自動計算する
- 端末描画は「ピクセル」ではなく「文字セル」で扱う

## 設計判断

- 最初の対象文字は ASCII を優先する
  - UTF-8 は後続へ送る
- surface と renderer を分離する
  - surface は pure logic、renderer は framebuffer 依存に分ける
- dirty tracking を最初から入れる
  - 全面再描画だけで固定すると後で性能が苦しくなる
- Plan 06 で userland terminal が使えるよう、ロジックは再利用しやすい形に保つ
  - 配置は `src/display/` でもよいが、ハードウェア非依存部は host test 前提にする

## 実装ステップ

1. 8x16 または 9x16 の固定幅フォントを組み込む
2. `struct term_cell { ch, fg, bg, attr }` と `terminal_surface` を定義する
3. `surface_clear`, `surface_putc`, `surface_scroll`, `surface_resize` を実装する
4. `draw_glyph(x, y, fg, bg, glyph)` とカーソル描画を実装する
5. dirty cell / dirty rect の tracking を導入する
6. surface 全体または差分だけを framebuffer に反映する cell renderer を作る
7. 行折り返し、タブ、スクロールの境界条件を host test で固定する

## 変更対象

- 新規候補
  - `src/display/font8x16.c`
  - `src/include/font.h`
  - `src/display/cell_renderer.c`
  - `src/include/cell_renderer.h`
  - `src/display/terminal_surface.c`
  - `src/include/terminal_surface.h`
  - `tests/test_terminal_surface.c`

## 検証

- `80x25` 以外の列数・行数で文字列を描画できる
- スクロールとカーソル移動の host test が通る
- dirty tracking により更新セルだけ再描画できる

## 完了条件

- 80x25 以外の列数・行数で文字を表示できる
- 色付き文字、カーソル、スクロールがセル単位で描画できる
- terminal client から使う最小 API が揃う
