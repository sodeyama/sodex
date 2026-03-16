# terminal / vi / ssh ちらつき調査メモ

**調査日**: 2026-03-17  
**対象**: local framebuffer terminal、`vi`、SSH PTY 経路  
**目的**: terminal や `vi` で入力時に発生するちらつきの根本原因を、repo 現状と Web 一次資料を突き合わせて整理し、改善方針を決める

---

## 1. 結論

- local の `term` は「damage tracking 自体」は入っているが、描画は front buffer へ直接・セル単位で行っており、back buffer / present 境界がないため、更新途中がそのまま見えてしまう構造になっている
- local のスクロールは `terminal_surface_scroll_up()` が画面全体を dirty にするため、1 行 scroll でも framebuffer 全面再描画になりやすい
- `vi` は 1 キー入力ごとに `ESC[2J ESC[H` で画面を消して全行を書き直す実装で、差分更新ではない。これが local / ssh の両方で最大の flicker 要因
- SSH はさらに悪く、`vi` の full redraw 出力を `sshd` が 1 tick あたり最大 128 raw byte 程度ずつしか PTY から吸い上げず、100Hz tick で小分け送信しているため、blank 画面や途中フレームが client に見えやすい
- 根本対策の優先順位は、`vi` の差分描画化、local `term` の back buffer 化と scroll blit、SSH の出力 coalescing 強化、必要なら synchronized output 対応、の順が妥当

このうち「100Hz tick なので full redraw が複数フレームに分割される」という部分は、repo 現状からの推論を含む。

---

## 2. repo 現状の観察

### 2.1 local terminal (`/usr/bin/term`)

`src/usr/command/term.c` の main loop は、PTY 出力をまとめて読み、入力もまとめて処理し、その後に 1 回 `render_surface()` を呼ぶ構造になっている。

- `pump_master()` は `TERM_PTY_READ_CHUNK=512`、`TERM_PTY_READ_BATCH=8192` で PTY を drain してから描画する
- つまり local の `term` は、`vi` の 1 回の再描画が 8KiB 未満なら「VT sequence の途中」ではなく「ある程度まとまった単位」で描画できる

一方で、`render_surface()` の framebuffer 経路は以下の性質を持つ。

- dirty cell を順に `cell_renderer_draw_cell()` で front buffer へ直接描く
- `fb_flush()` は空実装で、描画完了後に swap/present する概念がない
- カーソル描画も最後に再度セルを塗り直している
- IME overlay も別途 front buffer へ直接重ね描きしている

`cell_renderer_draw_cell()` 自体も、

1. 背景矩形を塗る
2. glyph を前景色で描く

という順で 1 セルずつ進むため、更新途中が見えると「列やセルが先に消えてから文字が乗る」見え方になりやすい。

### 2.2 local terminal の scroll

`src/usr/lib/libc/terminal_surface.c` の `terminal_surface_scroll_up()` は、logical surface 上では正しいが、描画コストの観点では厳しい。

- scroll 時に cell 配列を詰め直す
- 上から下まで `dirty=1` にする
- `dirty_count = cols * rows` にしている

その結果、`render_surface()` は scroll を検知しても fast path を持たず、metrics を出すだけで画面全体を描き直す。

repo には別系統で `src/display/fb_backend.c` の `fb_blit()` を使った scroll 実装がすでにあるが、userland `term` の render path では使っていない。  
つまり「既存 repo 内に scroll blit の発想はあるが、現行 `term` hot path に乗っていない」状態である。

### 2.3 `vi`

`src/usr/lib/libc/vi_screen.c` の `vi_screen_redraw()` は、毎回先頭で以下を出している。

- `ESC[0m`
- `ESC[2J`
- `ESC[H`

その後、

- 各表示行を先頭から再出力
- 行末で `ESC[K`
- 最下段 status line を再出力
- 最後にカーソル位置を再設定

としており、完全に full redraw 前提である。

さらに `src/usr/command/vi.c` では event loop のたびに

1. `vi_screen_redraw()`
2. `read()`
3. 入力処理

を繰り返しているため、入力 1 回ごとに全画面 clear + 再描画が発生する。

local `term` では PTY batch があるので多少救われるが、front buffer 直描きなので更新の途中は見える。  
SSH ではこの full redraw がそのまま client terminal に流れるため、最も悪化する。

### 2.4 SSH 経路

SSH 側の問題は `src/net/ssh_server.c` に集中している。

#### 入力方向

- `SSH_MSG_CHANNEL_DATA` を受けると 1 byte ずつ `tty_master_write()` している
- canonical/raw いずれでも TTY 側の処理は byte 単位

入力側の主問題は flicker そのものより syscall / ring / wakeup 粒度の細かさである。

#### 出力方向

出力側はより深刻で、`ssh_pump_tty_to_channel()` が

- `cooked_chunk[256]`
- `raw_cap = cap / 2`
- `raw_chunk[128]`

という上限で PTY から読むため、1 回の tick で吸い上げる redraw 量がかなり小さい。

加えて userland `sshd` は `sleep_ticks(1)` ベースで回っており、kernel 側の `HZ` は `100` なので 1 tick は約 10ms である。  
このため、たとえば 2KiB の `vi` redraw でも理論上は 16 回前後、4KiB なら 32 回前後に分割されうる。  
これは「画面 clear は先に届くが、全行の再描画が数十 ms から数百 ms に分散する」ことを意味し、SSH でちらつきが特に目立つ理由として整合する。

この見積もりは repo 内の定数と loop 構造からの推論であり、実測値ではない。

---

## 3. 外部資料から確認したこと

### 3.1 `ESC[2J` は本当に画面全消去である

xterm の control sequence 一覧では `CSI Ps J` は Erase in Display で、`Ps=2` は画面全体の消去である。  
また `CSI ? 1049 h/l` は alternate screen buffer の入退場に使われる。

つまり現行 `vi` は、

- alternate screen へ入る
- 毎入力で画面全消去
- 全行再出力

という、もっとも flicker しやすい系統の VT 出力をしている。

### 3.2 curses 系は virtual screen と physical screen を分け、最後にまとめて反映する

ncurses `curs_refresh(3X)` は、複数 window の更新を `wnoutrefresh()` で virtual screen に溜め、最後に `doupdate()` で physical screen 差分だけを反映する設計を説明している。  
man page でも、この分離は「複数 refresh によるちらつき低減」に有効だとしている。

ここからの判断:

- `vi_screen_redraw()` の `ESC[2J` 全消去は、curses が避けようとしているパターンそのもの
- Sodex 側でも `vi` 専用の virtual screen を持ち、dirty row / dirty span だけ出す設計に寄せるべき

### 3.3 synchronized output は remote full-screen redraw の途中表示を抑える

Contour の synchronized output 仕様では、

- `CSI ? 2026 h` で同期更新開始
- `CSI ? 2026 l` でその frame を一括反映

としている。  
説明でも「高頻度更新では tearing が起きるので、それを避けるために、更新中は旧画面を表示し続け、終了時に新しい画面を出す」としている。

WezTerm の公式 escape sequence 文書でも DECSET 2026 による synchronized rendering を明記している。  
iTerm2 の feature reporting 文書でも `SYNC` capability を公開しており、対応 terminal では feature detection が可能である。

ここからの判断:

- SSH client 側が対応していれば、`vi` redraw を `CSI ? 2026 h` / `l` で囲むだけでも「途中フレームが見える」問題はかなり軽くできる
- ただしこれは local framebuffer `term` には効かない。local は別に back buffer / present 境界が必要
- したがって synchronized output は SSH 専用の補助策であり、根本対策の全部ではない

### 3.4 double buffering は flicker 回避の定石である

X.org の DBE 拡張文書でも、double buffering は animation や complex repaint 時の flicker を防ぐための標準的手法として扱われている。

ここからの判断:

- local `term` の front buffer 直描きは、まさに double buffering 不在の設計
- `fb_flush()` が no-op のままでも、userland 側で off-screen cell bitmap を持って最後にまとめて blit するだけで、視覚上の改善は大きい

---

## 4. 根本改善の優先順位

### 4.1 最優先: `vi` を差分描画へ変える

現行の最大要因は `vi_screen_redraw()` の full clear である。  
ここを変えない限り、local でも SSH でも redraw 量が大きすぎる。

最小方針:

1. `ESC[2J` をやめる
2. 前回描画内容を `vi` 側で保持する
3. dirty row 単位で `ESC[row;colH` + 差分 span だけ更新する
4. status line と cursor move を独立管理する

理想形:

- `wnoutrefresh()` / `doupdate()` 相当の virtual screen / physical screen 方式
- 行 dirty だけでなく span dirty まで持つ
- scroll は `delete line` / `insert line` 系 VT を使えるとさらによい

### 4.2 local `term`: back buffer / present 境界を入れる

`term` は VT 解釈まではまともだが、描画が front buffer 直叩きなのが問題である。

優先度の高い順に:

1. cell 単位描画結果を off-screen pixel buffer に出す
2. dirty rect をまとめて framebuffer に copy する
3. 可能なら `fb_flush()` を swap/present 点として意味づける

最初は「全画面 back buffer + dirty rect copy」だけでもよい。  
per-cell `fill_rect -> glyph` が直接見えなくなるだけで、入力時のチラつきは大きく下がる。

### 4.3 local `term`: scroll fast path を入れる

現状は 1 行 scroll でも全 dirty なので、長文出力や `vi` のスクロールで負荷とちらつきが増える。

改善案:

1. `scroll_delta` が 1 以上のとき、既存 front/back buffer を `fb_blit()` 相当で上へ詰める
2. 新しく露出した最下段だけ描く
3. logical surface の dirty は維持しても、renderer 側で scroll fast path を優先する

repo 内に `fb_blit()` と `fb_backend_scroll_up()` があるので、ゼロから考える必要はない。

### 4.4 SSH: 出力 coalescing を local `term` 並みに寄せる

SSH 側は small chunk 化が強すぎる。

改善案:

1. `ssh_pump_tty_to_channel()` の read cap を引き上げる
2. 1 tick で 1 回だけでなく、「PTY が空になるか outbox/window が尽きるまで」複数回 drain する
3. CRLF 変換後の buffer も 256 byte 固定をやめる
4. packet 化は MTU と peer max packet を見つつ、少なくとも現在より大きくまとめる

local `term` が 8KiB まで drain してから描画しているのに対し、SSH は 128 raw byte 単位で止まっている。  
この差が remote 体験の悪化要因として大きい。

### 4.5 SSH 補助策: synchronized output を使う

client terminal が対応している場合、`vi` の redraw を

- `CSI ? 2026 h`
- redraw 本体
- `CSI ? 2026 l`

で囲むと、途中フレーム露出をかなり抑えられる可能性がある。

ただし注意点:

- 対応していない terminal では無視または非対応判定になる
- local framebuffer `term` には無関係
- 出力量そのものは減らない

よって「SSH 体験をすぐ改善する追加策」としては有効だが、`vi` 差分描画の代替にはならない。

---

## 5. 実装順の提案

最短で体感改善を出すなら次の順がよい。

1. `vi_screen_redraw()` の全消去廃止
2. `vi` の dirty row / dirty span 管理
3. SSH の PTY drain / packet coalescing 拡大
4. `vi` の synchronized output 対応
5. `term` の back buffer 化
6. `term` の scroll blit fast path

理由:

- `vi` full redraw は local/ssh 共通の最大要因
- SSH は user 体感が最も悪化しやすいので、coalescing を早めに入れる価値が高い
- local `term` の back buffer 化は根本策だが、`vi` full redraw を残したままでも無駄描画は多い

---

## 6. 追加で入れたい計測

現状の `TERM_METRIC` は redraw 回数や dirty cell 数は取れているが、frame 完了までの時間が見えない。

次を追加したい。

- `term`
  - 1 frame あたり render 開始 tick / 終了 tick
  - scroll fast path が使われた回数
  - dirty cell 数と dirty rect 面積
- `vi`
  - 1 redraw で出した byte 数
  - dirty row 数
  - full redraw fallback 回数
- `ssh`
  - 1 tick で PTY から読んだ byte 数
  - 1 redraw が何 packet / 何 tick に割れたか
  - `CHANNEL_DATA` 平均長

これがあると、「体感が良くなった」ではなく「1 入力あたり何 cell / 何 byte / 何 tick 改善したか」で追える。

---

## 7. 参考資料

一次資料を優先した。

- xterm control sequences: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
- ncurses `curs_refresh(3X)`: https://invisible-island.net/ncurses/man/curs_refresh.3x.html
- Contour terminal, synchronized output: https://contour-terminal.org/vt-extensions/synchronized-output/
- WezTerm escape sequences: https://wezterm.org/escape-sequences.html
- iTerm2 feature reporting: https://iterm2.com/feature-reporting/
- X.org DBE / double buffering: https://www.x.org/archive/X11R7.5/doc/man/man3/DBE.3.html

---

## 8. 補足

外部資料が直接教えてくれるのは、

- 全消去は flicker を招きやすい
- virtual screen / diff update が有効
- synchronized output は remote redraw の途中表示を抑えられる
- double buffering は描画途中露出を防ぐ

という一般則までである。  
「Sodex では `vi` full redraw と SSH 128-byte drain が特に致命的」という判断は、repo 現状コードと資料を突き合わせた設計上の推論である。
