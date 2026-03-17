# Plan 06: 検証と回帰固定化

## 概要

terminal view performance の改善は見た目だけでは判断しづらい。
host unit test と QEMU smoke をセットで整備し、
「ちらつき改善」と「既存機能非破壊」を両方固定する。

## 対象ファイル

- `tests/test_cell_renderer.c` — back buffer / present 周辺
- `tests/test_terminal_surface.c` — scroll metadata と dirty 管理
- `tests/test_vi_screen.c` — 新規。差分描画と fallback
- `src/test/run_qemu_terminal_smoke.py` — local term の確認
- `src/test/run_qemu_vi_smoke.py` — vi redraw の確認
- `src/test/run_qemu_ssh_smoke.py` — SSH 経路の確認

## 検証方針

### host unit test（TVP-24）

pure logic で固められるものは host 側に寄せる。

- `vi_screen` の dirty row / dirty span 検出
- full redraw fallback 条件
- `cell_renderer` の back/front 切り替えと present 範囲
- `terminal_surface_scroll_up()` の露出行 dirty と scroll metadata

### local term / vi smoke（TVP-25）

QEMU 上で次を確認する。

- `vi` 操作時に毎回 `ESC[2J` が出続けないこと
- `TERM_METRIC` で full redraw から partial redraw 優勢へ寄ること
- scroll fast path が使われ、full fallback が常態化しないこと

### SSH smoke（TVP-26）

既存の login / prompt / command 実行 / reconnect を壊さないことに加え、
vi redraw が過度に tick / packet 分割されないことを確認する。

frame marker を使う場合は、
`packets_per_frame` と `ticks_per_frame` を serial log から読めるようにする。

## 実装ステップ

1. TVP-24: host unit test 追加 / 更新
2. TVP-25: local term / vi smoke 更新
3. TVP-26: SSH smoke 更新

## リスク

- 見た目改善を数値 1 つで表しきれない
  - 対策: screenshot / serial metric / 接続回帰を組み合わせる
- test が flake しやすい
  - 対策: 厳しすぎる絶対閾値ではなく、改善方向が分かる相対条件を優先する
