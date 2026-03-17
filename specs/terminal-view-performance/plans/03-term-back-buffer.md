# Plan 03: local term の back buffer 化

## 概要

`term` の framebuffer 描画を front buffer 直書きから、
`term.c` 管理の back buffer + present へ変える。

## 対象ファイル

- `src/usr/command/term.c`
- `src/usr/command/makefile`

## 実装要点

- `struct cell_renderer` の ABI は変えない
- `term.c` で back buffer を確保し、renderer 複製時に `fb.base` を差し替える
- dirty rect を集約して back buffer から front buffer へ copy する
- viewport resize や console fallback 時に back buffer を作り直す
- `term` バイナリ肥大化で起動不能にならないよう `term.c` だけ `-Os` でビルドする

## 状態

完了。
