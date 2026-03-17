# Terminal View Performance

terminal / vi / SSH 経路のちらつきと redraw 分割を抑える改善の記録。

## 状態

この spec の内容は current repo に実装済み。
主な反映先は次のとおり。

- `src/usr/lib/libc/vi_screen.c` の可視フレーム差分描画
- `src/net/ssh_server.c` の PTY drain 強化と `SSH_METRIC`
- `src/usr/command/term.c` の back buffer / present / scroll fast path
- `src/usr/lib/libc/terminal_surface.c` の scroll dirty 管理修正
- `tests/test_vi_screen.c` ほか host test
- `src/test/run_qemu_terminal_smoke.py`
- `src/test/run_qemu_vi_smoke.py`
- `src/test/run_qemu_ssh_smoke.py`

## 背景

改善前の主な問題は 3 つだった。

### vi の全画面再描画

`vi_screen_redraw()` が毎回 `ESC[2J` を含む全消去と全行再出力を行い、
local / SSH ともに flicker の最大要因になっていた。

### local term の front buffer 直描き

framebuffer 経路の `term` は front buffer へ直接セル描画しており、
scroll でも広い範囲が見えてから埋まる状態になっていた。

### SSH 出力の細かすぎる分割

`ssh_pump_tty_to_channel()` が 1 tick あたり少量しか PTY を drain せず、
vi redraw が多 tick / 多 packet に分割されていた。

## 実装方針

### vi

raw line ではなく、`row_offset`、表示幅、反転状態、status / command 行を反映した
可視フレームを `vi_screen.c` 内部に保持し、前回フレームとの差分だけを出力する。

### local term

`cell_renderer` の ABI は変えず、`term.c` 側で back buffer を持つ。
描画時は renderer を複製して `fb.base` を back buffer に差し替え、
dirty rect だけ front buffer に present する。

### scroll fast path

既存の `surface.scroll_count` と `last_scroll_count` を使い、
back buffer 上の pixel 領域を `memmove()` してから露出行だけ描画する。

### SSH

PTY が空になるか window / outbox / byte budget が尽きるまで 1 tick 内で drain し、
`CSI ? 2026 h/l` を frame marker として `SSH_METRIC` で観測する。

## ゴール

- vi の 1 操作で画面全体が消えない
- local term の scroll が full redraw ではなく fast path に入る
- SSH の redraw が少ない packet / tick で届く
- host test と QEMU smoke で回帰を追える
