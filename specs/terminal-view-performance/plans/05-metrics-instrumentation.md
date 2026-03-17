# Plan 05: 計測基盤の整備

## 概要

flicker 改善の効果を「体感」ではなく定量データで追えるようにするため、
term / vi / SSH の各レイヤにメトリクスを追加し、serial log に出力する。

## 対象ファイル

- `src/usr/command/term.c` — render メトリクス
- `src/usr/lib/libc/vi_screen.c` — redraw メトリクス
- `src/net/ssh_server.c` — SSH 出力メトリクス
- `src/test/run_qemu_vi_smoke.py` / `run_qemu_terminal_smoke.py` / `run_qemu_ssh_smoke.py` — 取得と確認

## 計測項目

### term（TVP-19, TVP-20）

| メトリクス | 説明 | 追加場所 |
|---|---|---|
| `render_start_tick` | frame 描画開始の tick | `render_surface()` 先頭 |
| `render_end_tick` | frame 描画終了の tick | `render_surface()` 末尾 |
| `dirty_cell_count` | 1 frame で描画した dirty cell 数 | dirty loop 内カウンタ |
| `present_copy_area` | front へ copy した総ピクセル面積 | present 関数内 |
| `scroll_fast_path_count` | scroll fast path が使われた累計回数 | fast path 分岐内 |
| `scroll_full_fallback_count` | scroll fast path から全 redraw に落ちた回数 | fallback 分岐内 |

### vi（TVP-21）

| メトリクス | 説明 | 追加場所 |
|---|---|---|
| `redraw_bytes` | 1 redraw で出力した byte 数 | write 呼び出しの累計 |
| `dirty_row_count` | dirty と判定された行数 | dirty row 検出ループ |
| `dirty_span_count` | dirty span の総数 | span 出力ループ |
| `full_redraw_fallback` | full redraw に fallback した回数 | fallback 分岐内 |

### SSH（TVP-22）

| メトリクス | 説明 | 追加場所 |
|---|---|---|
| `pty_read_bytes_per_tick` | 1 tick で PTY から読んだ byte 数 | drain loop 内 |
| `channel_data_avg_len` | CHANNEL_DATA の平均長 | 送信時の移動平均 |
| `packets_per_frame` | frame marker がある redraw の packet 数 | marker 追跡ロジック |
| `ticks_per_frame` | frame marker がある redraw の tick 数 | marker 追跡ロジック |

## 出力方式（TVP-23）

- メトリクスは一定間隔（例: 100 frame ごと、または 10 秒ごと）で serial に出力
- フォーマット: `[METRIC] component: key=value, key=value, ...`
- 既存の `TERM_METRIC` 系出力をベースに component 名を揃える
- `build/log/serial.log` で確認可能

## 実装ステップ

1. TVP-19: term の frame メトリクス
2. TVP-20: scroll fast path カウンタ
3. TVP-21: vi の redraw メトリクス
4. TVP-22: SSH の出力メトリクス
5. TVP-23: serial log 出力の統合

## リスク

- メトリクス取得自体が性能に影響する
  - 対策: tick 取得は軽量なカーネル呼び出し、カウンタはインクリメントのみ
- serial 出力が大量になりすぎる
  - 対策: 出力間隔を調整可能にし、デフォルトは控えめに
- redraw 境界がないと SSH の `packets_per_frame` / `ticks_per_frame` は測れない
  - 対策: TVP-10 と依存づける
