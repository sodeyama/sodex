# Plan 05: 計測基盤の整備

## 概要

体感だけでなく serial log から改善を読めるようにする。

## 対象ファイル

- `src/usr/command/term.c`
- `src/usr/lib/libc/vi_screen.c`
- `src/net/ssh_server.c`
- `src/test/run_qemu_terminal_smoke.py`
- `src/test/run_qemu_vi_smoke.py`
- `src/test/run_qemu_ssh_smoke.py`

## 実装要点

- `term`: `present_copy_area`、`scroll_fast_path`、`scroll_full_fallback`
- `vi`: `redraw_bytes`、`dirty_rows`、`dirty_spans`、`full_fallbacks`
- `ssh`: `pty_read_bytes`、`channel_data_avg_len`、`packets_per_frame`、`ticks_per_frame`
- smoke script で数値フィールドの存在と最低限の活動量を確認する

## 状態

完了。
