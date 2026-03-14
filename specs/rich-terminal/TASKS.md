# Rich Terminal Tasks

`specs/rich-terminal/README.md` を、着手単位に落とした実装タスクリスト。
依存を崩さず順に進めることを前提にしている。

## 優先順

1. 互換を壊さない抽象化
2. graphics bring-up と文字セル描画
3. raw input と TTY/PTY
4. terminal client MVP
5. VT parser、resize、boot 統合
6. host/QEMU/perf の固定化

## M0: 互換を壊さない基盤化

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-01 | `display_backend` と console context を定義し、`_kputc()` / `_kprintf()` の互換層を作る | なし | 既存 boot log が維持されたまま backend 差し替え点ができる |
| [x] | RT-02 | `vga.c` のカーソル、スクロール、色処理を VGA text backend へ分離する | RT-01 | `src/vga.c` から VRAM 直操作の責務が減り、backend 実装へ寄る |
| [x] | RT-03 | `SCREEN_WIDTH` / `SCREEN_HEIGHT` と `sys_core.S` の固定値依存を backend 参照へ置き換える | RT-02 | 80x25 が console API の表面から消える |
| [x] | RT-04 | `key_event` 構造体、scan code 変換、event queue を導入する | RT-01 | raw key event を保持でき、従来の文字列入力と共存できる |
| [x] | RT-05 | 旧 `set_stdin()` / `get_stdin()` を key event からの互換 adapter に落とす | RT-04 | `eshell` は未変更でも動き、terminal client 用 raw 経路が別で使える |

## M1: Graphics bring-up

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | RT-06 | QEMU/Bochs 向け graphics device の検出と mode set を実装する | RT-03 | graphics mode に入れるか、失敗時に VGA text へ戻せる |
| [ ] | RT-07 | `fb_info`, `putpixel`, `fillrect`, `blit`, `flush` を持つ framebuffer 層を作る | RT-06 | 単色塗りつぶしや矩形描画ができる |
| [ ] | RT-08 | 8x16 系固定幅フォントと glyph 描画ルーチンを組み込む | RT-07 | 任意セル位置に ASCII 文字を描ける |
| [ ] | RT-09 | `term_cell` / `terminal_surface` / dirty tracking を実装する | RT-08 | 可変列数の surface 更新とスクロールが pure logic で扱える |
| [ ] | RT-10 | surface から framebuffer への cell renderer を作る | RT-09 | 80x25 を超える列数で文字列を描画できる |

## M2: Shell と terminal の分離

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-11 | `struct file` 周辺を見直し、stdio を特殊ケースではなく TTY 経路へ流せる形にする | RT-05 | `sys_read()` / `sys_write()` が TTY へ委譲できる |
| [x] | RT-12 | `tty` 本体、line discipline、canonical/raw の最小 subset を実装する | RT-11 | `ECHO` と行編集が TTY 側へ寄る |
| [x] | RT-13 | `pty master/slave` のペアとリングバッファを実装する | RT-12 | PTY 越しに双方向通信できる |
| [x] | RT-14 | shell 起動時に PTY slave を stdio へ割り当てる経路を作る | RT-13 | shell と console が分離される |
| [x] | RT-15 | raw input event を PTY へ流す最小 user/kernel API を定義する | RT-13 | terminal client が矢印キーや Ctrl を入力として送れる |

## M3: terminal client MVP

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-16 | `/usr/bin/term` の build、image 収録、起動骨格を作る | RT-10, RT-15 | `term` バイナリが build できる |
| [ ] | RT-17 | `term` から framebuffer と input event を初期化し、列数・行数を計算する | RT-16 | terminal 側で viewport が確定する |
| [x] | RT-18 | `term` が PTY master を開き、子として `eshell` を起動する | RT-14, RT-16 | shell 出力を読み込める |
| [ ] | RT-19 | `term` のメインループで PTY 読み取り、入力送信、damage redraw を回す | RT-17, RT-18 | shell 出力が framebuffer 上に見える |
| [ ] | RT-20 | scrollback リングと全画面再描画経路を追加する | RT-19 | 長文出力後も履歴を保持できる |

## M4: terminal としての完成度を上げる

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | RT-21 | VT parser 状態機械を実装し、通常文字と CSI を分離する | RT-19 | escape sequence を安全に解釈できる |
| [ ] | RT-22 | `clear`、色、カーソル移動、消去、保存/復元の主要シーケンスを実装する | RT-21 | `clear` と `ls --color` が通る |
| [ ] | RT-23 | 初期 winsize 伝播を `term` → shell に渡す | RT-18 | 120 列以上でも prompt が破綻しにくい |
| [ ] | RT-24 | `src/usr/init.c` を `term` 起動中心へ切り替え、旧 `eshell` を fallback 化する | RT-20, RT-22, RT-23 | boot 後の既定 terminal が `term` になる |
| [ ] | RT-25 | resize 通知と再描画を実装し、`eshell` の固定長前提を減らす | RT-23, RT-24 | 画面サイズ変更後も再計算と再描画ができる |

## M5: テストと性能固定化

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | RT-26 | `tests/Makefile` に surface、VT、key、TTY の host test を追加する | RT-09, RT-12, RT-21 | pure logic の回帰テストが一括実行できる |
| [ ] | RT-27 | VT fixture と期待 surface 比較のテストデータを用意する | RT-22, RT-26 | parser 退行を fixture で検知できる |
| [ ] | RT-28 | graphics terminal 向け QEMU smoke test を追加する | RT-24 | boot から shell 表示まで自動確認できる |
| [ ] | RT-29 | framebuffer dump または比較用スクリーンショット経路を用意する | RT-28 | 見た目差分を機械的に確認できる |
| [ ] | RT-30 | 全面再描画、1 行スクロール、長文出力の計測ポイントを入れる | RT-20, RT-28 | 描画ボトルネックを数値で比較できる |

## 先送りする項目

- UTF-8 完全対応
- 複数 terminal セッション
- タブ補完や高度な line editing
- マウス入力
- ウィンドウシステム
