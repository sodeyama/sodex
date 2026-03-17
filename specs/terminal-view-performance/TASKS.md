# Terminal View Performance Tasks

current repo で完了した作業単位の一覧。

## 優先順

1. vi の差分描画化
2. SSH 出力 coalescing 強化
3. local term の back buffer 化
4. local term の scroll fast path
5. 計測基盤の整備
6. host / QEMU 回帰確認の固定化

## M0: vi 差分描画化

| 状態 | ID | タスク | 完了条件 |
|---|---|---|---|
| [x] | TVP-01 | `vi_screen.c` に private な可視フレーム state を追加した | 前回描画結果を保持できる |
| [x] | TVP-02 | source 行ではなく可視フレームを組み立てるようにした | 差分判定が表示結果ベースになった |
| [x] | TVP-03 | `ESC[2J` 全消去を常用しない dirty row redraw に置き換えた | 変更のない行を再出力しない |
| [x] | TVP-04 | dirty row 内で dirty span を検出するようにした | 1 文字編集で該当 span だけ更新される |
| [x] | TVP-05 | status / command / cursor / fallback / restore を整理した | vi 起動終了と部分更新が両立する |

## M1: SSH 出力 coalescing 強化

| 状態 | ID | タスク | 完了条件 |
|---|---|---|---|
| [x] | TVP-06 | `ssh_pump_tty_to_channel()` の read cap と byte budget を拡張した | 1 tick で読める最大量が増えた |
| [x] | TVP-07 | PTY drain を loop 化した | PTY が空か予算尽きるまで 1 tick で送る |
| [x] | TVP-08 | CRLF 変換 workspace を広げた | 変換途中の分割が減った |
| [x] | TVP-09 | peer window / max packet / outbox を尊重して大きめに packet 化した | redraw の packet 数が減った |
| [x] | TVP-10 | `CSI ? 2026 h/l` を frame marker として扱い `SSH_METRIC` を出すようにした | SSH 側で redraw 境界を観測できる |

## M2: local term の back buffer 化

| 状態 | ID | タスク | 完了条件 |
|---|---|---|---|
| [x] | TVP-11 | `term.c` に back buffer と present 範囲管理を追加した | front buffer 直描きを避けられる |
| [x] | TVP-12 | viewport 変更に応じた back buffer の確保 / resize / 解放を入れた | resize 後も破綻しない |
| [x] | TVP-13 | dirty rect を front buffer へ copy する present を入れた | 1 frame がまとめて見える |
| [x] | TVP-14 | カーソルと IME overlay も back buffer 経由に寄せた | 重ね描きが present 手順に載る |

## M3: local term の scroll fast path

| 状態 | ID | タスク | 完了条件 |
|---|---|---|---|
| [x] | TVP-15 | `surface.scroll_count` と `last_scroll_count` で fast path に入るようにした | scroll を検出できる |
| [x] | TVP-16 | back buffer 内の pixel 領域を `memmove()` で詰めるようにした | 既存画面を再利用できる |
| [x] | TVP-17 | 露出した行と残り dirty cell だけ描画するようにした | scroll 時の描画量を抑えられる |
| [x] | TVP-18 | `terminal_surface_scroll_up()` の dirty 管理を修正した | scroll metadata と dirty が整合する |

## M4: 計測基盤の整備

| 状態 | ID | タスク | 完了条件 |
|---|---|---|---|
| [x] | TVP-19 | `term` に `present_copy_area` を追加した | present 量を計測できる |
| [x] | TVP-20 | `term` に scroll fast path / fallback カウンタを追加した | scroll 最適化の利用状況を追える |
| [x] | TVP-21 | `vi` に redraw byte / dirty row / dirty span / fallback メトリクスを追加した | vi の差分描画効率を追える |
| [x] | TVP-22 | SSH に PTY read byte / CHANNEL_DATA 平均長 / frame packet/tick を追加した | coalescing 効果を追える |
| [x] | TVP-23 | `TERM_METRIC` / `SSH_METRIC` を serial log で比較できる形に揃えた | smoke から検証できる |

## M5: 検証と回帰固定化

| 状態 | ID | タスク | 完了条件 |
|---|---|---|---|
| [x] | TVP-24 | `vi_screen` / `terminal_surface` / string 周辺の host test を追加更新した | pure logic を host で固定できる |
| [x] | TVP-25 | `run_qemu_terminal_smoke.py` と `run_qemu_vi_smoke.py` を更新した | local term / vi の metric を確認できる |
| [x] | TVP-26 | `run_qemu_ssh_smoke.py` を更新した | SSH 経路の metric と接続回帰を両立確認できる |
