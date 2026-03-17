# Plan 04: local term の scroll fast path

## 概要

`terminal_surface_scroll_up()` が全セルを dirty にする現状を改め、
scroll 時に既存画面を blit で上に詰め、新しく露出した行だけを描画する fast path を導入する。

## 対象ファイル

- `src/usr/lib/libc/terminal_surface.c` — `terminal_surface_scroll_up()`
- `src/usr/include/terminal_surface.h` — scroll metadata
- `src/usr/command/term.c` — `render_surface()` の scroll 分岐
- `tests/test_terminal_surface.c` — host 側の確認

## 現状の問題

- `terminal_surface_scroll_up()` が cell 配列を詰め直した後、全セルに `dirty=1` を設定
- `dirty_count = cols * rows` で全画面 dirty
- `render_surface()` はすでに `scroll_count` を見て metrics を出しているが、描画最適化には使っていない
- kernel 側には `fb_blit()` があるが、userland `term` はその経路を使っていない

## 設計

### 既存 `scroll_count` の活用（TVP-15）

新しい `scroll_delta` を増やすのではなく、
既存の `surface.scroll_count` と `last_scroll_count` の差分で fast path に入る。
必要なら「今回露出した下端行数」を追う最小限の metadata だけ追加する。

### blit による画面移動（TVP-16）

fast path では:
1. back buffer 上で `scroll_delta * cell_height` ピクセル分を上に `memmove`
2. front buffer は present 時にまとめて更新

back buffer 化（Plan 03）が前提。back buffer 内で完結するため front buffer への途中露出はない。

### 露出行の描画（TVP-17）

scroll 後に新しく見える最下行（`rows - scroll_delta` 〜 `rows - 1`）だけを描画する。
これらの行だけ `dirty=1` にし、他の行は `dirty=0` のままにする。

### dirty 管理の整合（TVP-18）

`terminal_surface_scroll_up()` の dirty 設定を変更:
- 全行 `dirty=1` ではなく、新規露出行のみ `dirty=1`
- `scroll_count` は従来どおり増やしつつ、fast path に必要な metadata を残す
- `render_surface()` 側で scroll 情報を消費後にリセット

## 実装ステップ

1. TVP-15: `scroll_count` 検出 + fast path 分岐
2. TVP-16: back buffer 内 blit
3. TVP-17: 露出行のみ描画
4. TVP-18: dirty 管理の整合

## リスク

- 複数行 scroll が連続した場合の delta 蓄積
  - 対策: delta が rows に近づいたら full redraw に fallback
- scroll と同時にセル内容も変わるケース（vi のスクロール等）
  - 対策: scroll fast path 後に残りの dirty cell を通常描画
- 露出行だけではなく status overlay やカーソル再描画も必要になる
  - 対策: fast path 後もカーソル・IME overlay は通常の present 手順に乗せる
