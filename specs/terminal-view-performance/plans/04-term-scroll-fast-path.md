# Plan 04: local term の scroll fast path

## 概要

scroll 時に全セルを描き直すのではなく、
既存画面を back buffer 上で詰めて露出行だけ描画する。

## 対象ファイル

- `src/usr/lib/libc/terminal_surface.c`
- `src/usr/command/term.c`
- `tests/test_terminal_surface.c`

## 実装要点

- 既存の `surface.scroll_count` と `last_scroll_count` を使って fast path を選ぶ
- back buffer の pixel 領域を `memmove()` で上に詰める
- 新しく露出した行だけ dirty にする
- fast path 利用回数と fallback 回数を `TERM_METRIC` で出す

## 状態

完了。
