# Plan 01: vi 差分描画化

## 概要

`vi_screen_redraw()` の全消去 + 全行再出力をやめ、
可視フレーム差分だけを VT sequence で出す。

## 対象ファイル

- `src/usr/lib/libc/vi_screen.c`
- `src/usr/include/vi.h`
- `tests/Makefile`
- `tests/test_vi_screen.c`

## 実装要点

- `vi_screen.c` 内部だけで前回フレームを保持する
- `row_offset`、wide char 幅、visual 反転、status / command 行を含む可視フレームを比較する
- dirty row と dirty span を検出して必要な部分だけ更新する
- 初回描画や不整合時だけ full redraw fallback を許す
- redraw 効率は `TERM_METRIC component=vi ...` で出す

## 状態

完了。
