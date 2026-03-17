# Terminal View Performance Spec

terminal / vi / SSH 経路で発生するちらつき（flicker）を根本的に解消するための計画。

## 背景

調査ドキュメント: `docs/research/terminal_vi_ssh_flicker_research_2026-03-17.md`

現状の問題は 3 つのレイヤに跨がる。

### vi の全画面再描画

`vi_screen_redraw()` が毎入力で `ESC[2J` + `ESC[H` を発行し、画面全体を消去してから全行を再出力する。
curses が避けようとしているパターンそのものであり、local / SSH 双方で最大の flicker 要因。

### local term の front buffer 直描き

`term` の framebuffer 経路は `get_fb_info()` で得た front buffer を
`cell_renderer_draw_cell()` が直接更新しており、userland 側に present 境界がない。
加えて `terminal_surface_scroll_up()` は scroll 時に全セルを dirty にするため、
1 行 scroll でも全面再描画になりやすい。

### SSH 出力の過小 coalescing

`ssh_pump_tty_to_channel()` が 1 tick あたり最大 128 raw byte しか PTY から吸い上げない。
kernel の HZ=100 で 1 tick=10ms のため、2KiB の vi redraw でも 16 tick（160ms）に分割される。
画面 clear が先に届き、再描画が数十〜数百 ms に分散することで SSH 体験が最も悪化する。

## 既存実装で活かす前提

- `term` にはすでに `TERM_METRIC`、`scroll_count`、dirty cell 管理がある
- `terminal_surface` には alternate screen と scroll 回数の追跡がある
- `vt_parser` は `?1049` は処理するが、`?2026` は未対応 sequence として無視する
- `term` の framebuffer 経路は kernel の `fb_backend` ではなく userland `cell_renderer` を使っている

この spec は、上記の既存機構を踏まえて最小差分で改善する方針を取る。

## ゴール

- vi で 1 キー入力した際のちらつきが local / SSH ともに視認できないレベルになる
- local term の scroll が全面再描画ではなく blit fast path で処理される
- SSH 経路の vi 操作体感が local に近づく
- 改善を定量的に計測できるメトリクスと回帰確認手順が整備される

## 非ゴール

- ncurses 互換ライブラリの完全実装
- GPU アクセラレーション
- vi 以外のフルスクリーンアプリケーション対応
- terminal capability negotiation の完全実装

## アーキテクチャ方針

### vi: 可視フレーム差分方式

raw の source line ではなく、`row_offset`、UTF-8 幅、visual 選択、status / command 行を反映した
「可視フレーム」を `vi_screen.c` 内部で構築し、前回フレームとの差分だけ VT sequence を出力する。
public API は `src/usr/include/vi.h` のまま保ち、`ESC[2J` 全消去を廃止する。

### local term: userland double buffering

off-screen pixel buffer（back buffer）を `term` / `cell_renderer` 側で持ち、
dirty rect だけを front buffer へ memcpy する。
kernel 側 `fb_flush()` の再定義には依存せず、present は userland 完結で入れる。

### local term: scroll_count を使った fast path

新しい `scroll_delta` を別途発明するのではなく、
既存の `surface.scroll_count` と `last_scroll_count` を起点に fast path へ入る。
必要に応じて「今回露出した最下行」を特定する最小限の補助メタデータだけを追加する。

### SSH: 出力 coalescing 強化 + 補助的な frame boundary

PTY drain の read cap を引き上げ、1 tick で PTY が空になるか
window / outbox / per-tick byte budget が尽きるまで複数回 drain する。
補助策として synchronized output（`CSI ? 2026 h/l`）または同等の frame marker を使うが、
unsupported terminal を壊さない gated な導入に留める。

## 実装フェーズ

1. **M0**: vi 差分描画化（最大効果・local/SSH 共通）
2. **M1**: SSH 出力 coalescing 強化（SSH 体験の即時改善）
3. **M2**: local term の back buffer 化（front buffer 直描きの解消）
4. **M3**: local term の scroll fast path（scroll 性能）
5. **M4**: 計測基盤の整備（定量評価）
6. **M5**: host / QEMU 回帰確認の固定化
