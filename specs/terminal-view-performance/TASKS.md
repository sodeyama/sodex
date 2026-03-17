# Terminal View Performance Tasks

`specs/terminal-view-performance/README.md` を、着手単位に落とした実装タスクリスト。
依存を崩さず順に進めることを前提にしている。

## 優先順

1. vi の差分描画化（local/SSH 共通の最大要因を解消）
2. SSH 出力 coalescing 強化（remote 体験の即時改善）
3. local term の back buffer 化（front buffer 直描きの解消）
4. local term の scroll fast path（scroll 性能）
5. 計測基盤の整備（定量評価）
6. host / QEMU 回帰確認の固定化

## M0: vi 差分描画化

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | TVP-01 | `vi_screen.c` に private な可視フレーム state を追加し、前回描画結果を保持する | なし | 本文・status・command 行を含む前回フレームを比較できる |
| [ ] | TVP-02 | raw line ではなく、`row_offset` / UTF-8 幅 / visual 選択を反映した可視フレームを組み立てる | TVP-01 | 差分判定の入力が source text ではなく表示結果になる |
| [ ] | TVP-03 | `vi_screen_redraw()` から `ESC[2J` 全消去を除去し、dirty row 検出ロジックを入れる | TVP-02 | 変更のない行には VT sequence を出さない |
| [ ] | TVP-04 | dirty row 内で dirty span を表示列ベースで検出し、`ESC[row;colH` + 差分 + 必要時 `ESC[K` を出す | TVP-03 | 1 文字編集で画面全体ではなく該当 span のみ更新される |
| [ ] | TVP-05 | status / command 行、cursor move、初回描画、full redraw fallback、終了時 restore を整理する | TVP-04 | vi 起動/終了や fallback を含めて画面状態が破綻しない |

## M1: SSH 出力 coalescing 強化

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | TVP-06 | `ssh_pump_tty_to_channel()` の read cap と per-tick byte budget を見直す | なし | 1 tick で読める最大量が増え、stack 使用量も制御できる |
| [ ] | TVP-07 | PTY drain を「PTY が空になるか outbox/window/byte budget が尽きるまで」の loop に変更する | TVP-06 | 1 tick 内で可能な限り output をまとめる |
| [ ] | TVP-08 | CRLF 変換バッファを固定 256 byte から可変 workspace に変更する | TVP-06 | 変換途中での分割が減る |
| [ ] | TVP-09 | SSH packet 化を peer window / max packet / outbox slot を尊重して大きくまとめる | TVP-07 | 1 redraw が少ない packet 数で送信される |
| [ ] | TVP-10 | synchronized output か同等の frame marker を gated に導入し、SSH 側メトリクスと補助描画抑止に使う | TVP-05, TVP-09 | 対応 terminal では途中フレーム露出を抑え、非対応環境を壊さない |

## M2: local term の back buffer 化

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | TVP-11 | `cell_renderer` を front/back 別バッファ対応にし、描画先を切り替えられるようにする | なし | `cell_renderer_draw_cell()` が front buffer に直接書かない |
| [ ] | TVP-12 | `term.c` に back buffer の確保・resize・解放を追加する | TVP-11 | viewport 変更後も back buffer が破綻しない |
| [ ] | TVP-13 | dirty rect を収集し、back buffer から front buffer へまとめて copy する userland present 処理を入れる | TVP-12 | 1 frame の描画が atomic に見える |
| [ ] | TVP-14 | カーソル描画と IME overlay を back buffer 経由に統合する | TVP-13 | カーソルや IME が front buffer に直接重ね描きしない |

## M3: local term の scroll fast path

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | TVP-15 | 既存の `surface.scroll_count` と `last_scroll_count` を使って fast path へ入る | TVP-13 | scroll 時に全 dirty ではなく scroll blit が試みられる |
| [ ] | TVP-16 | fast path で back buffer 内の pixel 領域を `memmove` / blit で上へ詰める | TVP-15 | 既存画面が移動し、全ピクセル再描画しない |
| [ ] | TVP-17 | 新しく露出した最下行と残り dirty cell だけを描画する | TVP-16 | scroll 時の描画コストが露出行 + 差分セルに抑えられる |
| [ ] | TVP-18 | `terminal_surface_scroll_up()` の全 dirty 化を見直し、露出行と scroll metadata を正しく残す | TVP-17 | logical surface の dirty が scroll fast path と整合する |

## M4: 計測基盤の整備

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | TVP-19 | `term` に frame 開始/終了 tick、dirty cell 数、present copy 面積のメトリクスを追加する | TVP-13 | frame 描画時間と copy 量が計測できる |
| [ ] | TVP-20 | `term` に scroll fast path 使用回数と full redraw fallback 回数を追加する | TVP-18 | fast path の利用頻度と fallback が追える |
| [ ] | TVP-21 | `vi` に redraw byte 数、dirty row/span 数、full redraw fallback 回数を追加する | TVP-04 | vi の描画効率が定量的に追える |
| [ ] | TVP-22 | SSH に 1 tick あたり PTY read byte 数、CHANNEL_DATA 平均長、frame marker 利用時の packet/tick 分割数を追加する | TVP-09, TVP-10 | SSH 出力 coalescing の効果が定量的に追える |
| [ ] | TVP-23 | 既存の `TERM_METRIC` 系出力を拡張し、serial log で横断的に確認できる形へ揃える | TVP-19, TVP-21, TVP-22 | `build/log/serial.log` から比較可能な形で読める |

## M5: 検証と回帰固定化

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | TVP-24 | `vi_screen` / `cell_renderer` / `terminal_surface` の host unit test を追加・更新する | TVP-18, TVP-21 | 差分描画・present・scroll fast path の pure logic を host で固定できる |
| [ ] | TVP-25 | `run_qemu_vi_smoke.py` と `run_qemu_terminal_smoke.py` を更新し、full redraw 減少と scroll fast path 利用を確認する | TVP-20, TVP-21, TVP-23 | local term / vi の体感改善を QEMU で再現確認できる |
| [ ] | TVP-26 | `run_qemu_ssh_smoke.py` を更新し、vi redraw の分割改善と login / prompt 回帰なしを確認する | TVP-10, TVP-22, TVP-23 | SSH 経路の改善と既存接続性を両立できる |
