# Plan 03: フォントと文字セルレンダラ

## 概要

framebuffer の上に terminal を作るため、ビットマップフォントと文字セル単位の描画器を作る。
ここで初めて「80桁固定ではない terminal」が成立する。

## 方針

- 初期実装は ASCII 中心の固定幅ビットマップフォント
- `cols = width / cell_width`, `rows = height / cell_height` で自動計算する
- 端末描画は「ピクセル」ではなく「文字セル」で扱う

## 実装ステップ

1. 8x16 または 9x16 の固定幅フォントを組み込む
2. `draw_glyph(x, y, fg, bg, glyph)` を実装する
3. `struct term_cell { ch, fg, bg, attr }` を定義する
4. カーソル描画、反転、下線、太字相当の表現を決める
5. 文字セル配列から dirty rect ベースで framebuffer に反映する

## 変更対象

- 新規候補
  - `src/display/font8x16.c`
  - `src/include/font.h`
  - `src/display/cell_renderer.c`
  - `src/include/cell_renderer.h`
  - `tests/test_terminal_surface.c`

## 完了条件

- 80x25 以外の列数・行数で文字を表示できる
- 色付き文字、カーソル、スクロールがセル単位で描画できる
- terminal client から使う最小 API が揃う

