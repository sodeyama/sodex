# Rich Terminal Spec

GUI は作らず、しかし現在の VGA テキストモード 80x25 より遥かに実用的な
ターミナルを実装するための計画。

狙いは「カーネルが直接 0xB8000 に文字を書く」構造を卒業し、
カーネルは描画基盤と TTY/PTY を提供し、ユーザー空間の terminal client が
描画・入力・VT 解釈を担う構成に移すこと。

## 背景

現状の制約:

- `src/vga.c` が VGA テキスト VRAM に直接書き込む
- 画面サイズは `80x25` に固定
- `sys_write()` は標準出力をそのまま `_kputc()` に流す
- `sys_read()` は `key.c` の文字列キューを直接読む
- `eshell` は 64 バイトの行入力前提で、通常のターミナルのような振る舞いを持たない

この構造では以下が難しい:

- 120x40 や 160x50 のような広い表示
- スクロールバック
- ANSI/VT シーケンス
- カーソル移動、色、クリア、再描画
- shell とは独立した terminal client

現状コードで強く結合している箇所:

- `src/vga.c` / `src/include/vga.h`
  - `_kputc()` と `_kprintf()` が描画とカーソル管理を抱えている
- `src/syscall.c`
  - `sys_write()` が標準出力を `_kputc()` へ直結している
  - `sys_read()` が `get_stdin()` を直接読んでいる
- `src/key.c` / `src/include/key.h`
  - 入力が「文字列キュー」であり、raw key event が存在しない
- `src/usr/init.c`
  - boot 後に `eshell` を直接起動している
- `src/usr/command/makefile` / `src/usr/makefile`
  - 新しい `term` バイナリを build と image に載せる導線がまだない

## ゴール

- 文字セルベースだが、任意の解像度から列数・行数を計算できる
- userland の terminal client が PTY master を持ち、shell は PTY slave 上で動く
- ANSI/VT100 の主要シーケンスを解釈できる
- スクロールバック、再描画、カーソル、色属性を扱える
- 既存の VGA テキストモードを捨てず、段階移行できる

## 非ゴール

- 汎用 GUI ウィンドウシステム
- マウス主体の UI
- 複数ウィンドウ管理
- GPU アクセラレーション
- M8 までの ASCII 中心実装における UTF-8 完全対応

## 目標アーキテクチャ

```
          ┌──────────────────────────────┐
          │ user: /usr/bin/term         │
          │  - VT parser                │
          │  - cell surface             │
          │  - scrollback               │
          │  - framebuffer renderer     │
          │  - keyboard event handling  │
          └──────────────┬───────────────┘
                         │ PTY master
          ┌──────────────┴───────────────┐
          │ kernel                        │
          │  - PTY / TTY / line discipline│
          │  - input event queue          │
          │  - display backend API        │
          │  - framebuffer driver         │
          └──────────────┬───────────────┘
                         │ PTY slave
          ┌──────────────┴───────────────┐
          │ user: eshell / app           │
          └──────────────────────────────┘
```

## 実装ポリシー

- Big Bang では置き換えない
  - まず既存の `_kputc()` / `sys_read()` / `sys_write()` を互換 layer 化し、各段階で fallback を残す
- terminal state は最終的に userland が持つ
  - kernel は framebuffer、input event、TTY/PTY、winsize 伝播だけを担う
- hardware 依存部と pure logic を分ける
  - VT parser、surface、ring buffer、key map は host test を先に書ける構造へ寄せる
- 初期実装は機能を絞る
  - ASCII、16 色、単一 full-screen terminal、単一 controlling TTY を先に成立させる

## Plans

| # | ファイル | 概要 | 依存 | この plan の出口 |
|---|---------|------|------|-------------------|
| 01 | [01-display-abstraction.md](plans/01-display-abstraction.md) | VGA 直書きを backend 抽象へ切り出す | なし | `_kputc()` 系が backend 経由になり、80x25 固定が表面から消える |
| 02 | [02-linear-framebuffer-driver.md](plans/02-linear-framebuffer-driver.md) | QEMU/Bochs 向け線形 framebuffer を使えるようにする | 01 | graphics mode で塗りつぶしと基本描画ができる |
| 03 | [03-font-and-cell-renderer.md](plans/03-font-and-cell-renderer.md) | ビットマップフォントと文字セル描画を実装する | 02 | 可変列数の文字セル描画と surface 操作が揃う |
| 04 | [04-input-event-pipeline.md](plans/04-input-event-pipeline.md) | 文字列キューから key event 方式へ移行する | 01 | raw key event と従来文字入力を両立できる |
| 05 | [05-tty-pty-layer.md](plans/05-tty-pty-layer.md) | 標準入出力を TTY/PTY ベースに再設計する | 04 | shell と表示を分離し、PTY 越しに stdio を流せる |
| 06 | [06-terminal-client-core.md](plans/06-terminal-client-core.md) | userland terminal client を導入する | 03, 04, 05 | `/usr/bin/term` が起動し、shell 出力を描画できる |
| 07 | [07-vt-parser.md](plans/07-vt-parser.md) | ANSI/VT100 の主要シーケンスを解釈する | 06 | `clear`、色、カーソル移動が破綻せず動く |
| 08 | [08-shell-integration-and-resize.md](plans/08-shell-integration-and-resize.md) | eshell 起動、列数伝播、再描画と移行を整える | 05, 06, 07 | boot 後の既定経路が `term` になり、winsize と resize が通る |
| 09 | [09-testing-and-benchmarks.md](plans/09-testing-and-benchmarks.md) | テスト、QEMU 検証、描画性能計測を整える | 全体横断 | 回帰テストと性能計測の土台が揃う |
| 10 | [10-fs-crud-and-basic-commands.md](plans/10-fs-crud-and-basic-commands.md) | ファイル/フォルダ CRUD、`touch`、`mkdir`、`rm` などの基本コマンドと syscall を整える | 08, 09 | shell 上で最小 CRUD が成立する |
| 11 | [11-shell-pipes-and-redirection.md](plans/11-shell-pipes-and-redirection.md) | shell の `|`, `>`, `<` と pipe / fd 制御を整える | 08, 09, 10 | shell 上で最小 I/O 合成が成立する |
| 12 | [12-vi-file-creation.md](plans/12-vi-file-creation.md) | `term` 上で `vi` により新規ファイルを作成・保存できるようにする | 07, 08, 09, 10, 11 | `vi memo.txt` を開いて `:wq` で保存できる |
| 13 | [13-utf8-support.md](plans/13-utf8-support.md) | terminal / shell / vi を UTF-8 と `UDEV Gothic` 由来の多言語表示へ拡張する | 03, 07, 08, 09, 12 | kernel 既定フォントで日本語を含む UTF-8 テキストを表示・編集・保存できる |
| 14 | [14-japanese-input.md](plans/14-japanese-input.md) | `term` に guest 内 IME を追加し、日本語直接入力と mode 切り替えを成立させる | 04, 05, 08, 09, 13 | shell / `vi` で日本語を直接入力し、UTF-8 のまま保存できる |

## マイルストーン

| マイルストーン | 対象 plan | 到達状態 |
|---------------|-----------|----------|
| M0: 互換を壊さない基盤化 | 01, 04 | 表示 backend 抽象と raw input event が入り、現行 console がそのまま動く |
| M1: Graphics bring-up | 02, 03 | framebuffer と文字セル描画が入り、80x25 を超える表示ができる |
| M2: Shell と terminal の分離 | 05, 06 | PTY 越しに shell を動かし、`term` で表示と入力を扱える |
| M3: terminal としての最低限完成 | 07 | ANSI/VT の主要シーケンス、scrollback、再描画が通る |
| M4: 既定 boot 経路への統合 | 08 | init から `term` 起動、winsize 伝播、resize が成立する |
| M5: 壊れにくい状態へ固定 | 09 | host/QEMU/perf の検証経路が揃う |
| M6: shell 上の基本ファイル操作成立 | 10 | `touch`、`mkdir`、`rm`、`rmdir`、`mv` と対応 syscall が揃う |
| M7: shell の I/O 合成立 | 11 | `|`, `>`, `<` と pipe / fd 制御が揃う |
| M8: フルスクリーン editor の成立 | 12 | `term` 上で `vi` により新規ファイルを作成して保存できる |
| M9: UTF-8 と多言語表示 | 13 | `UDEV Gothic` 由来の既定フォントパックを kernel に載せ、日本語を含む UTF-8 テキストを shell / `vi` / terminal で破綻なく扱える |
| M10: 日本語直接入力 | 14 | `term` に最小 IME が入り、日本語の切り替えと直接入力が shell / `vi` で成立する |

## 現在の到達点

- M10 まで実装済み
- shell 上で `touch`、`mkdir`、`rm`、`rmdir`、`mv` が動作する
- shell 上で `|`、`>`、`<` が動作する
- `term` 上で `vi memo.txt` を開き、`i` で編集して `:wq` で保存できる
- `UDEV Gothic` 由来の既定フォントパックを kernel / `term` の両方で使い、日本語を含む UTF-8 テキストを表示できる
- `cat utf8.txt` と `vi utf8.txt` で、日本語を含む UTF-8 ファイルの表示・編集・保存ができる
- host test と QEMU smoke で CRUD、shell I/O、`vi`、UTF-8 保存導線を固定している
- `term` で `Ctrl+Space` により `latin` / `hiragana` / `katakana` を切り替えられる
- shell で日本語 filename を直接入力でき、`vi` insert mode でも日本語を直接入力して保存できる
- host test と QEMU smoke で IME の mode 切り替え、日本語入力、UTF-8 保存を固定している
- 漢字変換、候補 UI、予測変換は未対応で、現状はかな直接入力を対象にしている

## 実装順序

1. まず VGA 直書きを抽象化し、表示 backend を差し替え可能にする
2. 次に QEMU/Bochs 向けの線形 framebuffer と文字セルレンダラを作る
3. その上で入力を key event 化し、TTY/PTY を挟んで shell と terminal を分離する
4. terminal client に VT parser、scrollback、resize を載せる
5. 次に boot path と terminal のテストを固める
6. その上で file/folder CRUD と基本コマンドを入れ、shell から namespace を操作できるようにする
7. その次に pipe / redirection を入れ、shell で `|`, `>`, `<` を扱えるようにする
8. その上で `vi` を導入し、フルスクリーン編集と保存を成立させる
9. その次に UTF-8 decoder、表示幅計算、`UDEV Gothic` 由来の既定フォントパックの kernel 組み込みを入れて多言語表示へ広げる
10. 最後に `term` 常駐 IME と UTF-8 入力経路を入れ、日本語直接入力へ広げる

## 並行化の考え方

- `01` 完了後に `02` と `04` は並行化しやすい
- `03` は `02` の bring-up 完了後に着手する
- `05` は `04` を前提に進めるが、`file_ops` / `tty buffer` の設計だけ先行できる
- `06` は `03`, `04`, `05` が最低限揃った時点で MVP を開始できる
- `09` は横断だが、host test の足場だけは `03`, `04`, `05`, `07` と同時に進める
- `10` は `08`, `09` 完了後に shell 実運用向けの file 操作として進める
- `11` は `10` の CRUD が通った後に shell I/O 合成として進める
- `12` は `11` 完了後に着手する
- `14` は `13` 完了後に着手し、input 系は `04` / `05` の延長として進める

## 主な設計判断

- VGA text backend はすぐには消さない
  - graphics mode 失敗時の fallback とテスト比較対象として残す
- framebuffer は terminal 機能を持たない
  - `putpixel`, `fillrect`, `blit`, `flush` だけを持つ低レイヤに留める
- TTY/PTY は最小構成から始める
  - まずは `ICANON`, `ECHO`, `VINTR` 相当の最小 subset を目標にする
- resize の初回伝播は簡易でよい
  - 初期値は `COLUMNS` / `LINES` か winsize 構造体を子プロセスへ渡し、後から `ioctl` / `SIGWINCH` 相当へ拡張する
- 初期の `vi` は自前の最小実装にする
  - 既存の VT subset と ext3 書き込み経路に合わせ、外部移植より先に編集フローを通す
- file CRUD は syscall と command を一緒に固める
  - shell から使えない機能を kernel にだけ先行実装しない
- shell の `|`, `>`, `<` は最初は単純形だけに絞る
  - 1 本の pipe、単一 redirection、quoting なしの最小構文から始める
- TrueType/OTF の解釈は kernel に持ち込まない
  - host build で `UDEV Gothic` をビットマップ化し、生成済みフォントパックを kernel の既定フォントとして読む
- `UDEV Gothic` は SIL Open Font License 1.1 前提で扱う
  - 配布時はライセンス文書と attribution を同梱し、生成済みフォントパックは別名で扱う
- 日本語直接入力は Plan 14 で `term` 常駐 IME として扱う
  - host IME の結果を guest へそのまま期待しない
  - 初期段階はかな入力を優先し、漢字変換は後段へ分ける
- 複数 terminal、複数ウィンドウは後回し
  - 先に単一 full-screen terminal を stable にする
- UTF-8 は Plan 13 でまとめて扱う
  - M8 までは ASCII 前提を維持し、M9 で文字幅、glyph、既定フォントパック問題をまとめて解く

## 未解決論点

- framebuffer 初期化を PCI の BGA で行くか、VBE 互換層を噛ませるか
- userland terminal から framebuffer へどうアクセスさせるか
  - 専用 syscall、共有メモリ、または描画 syscall のどれを採るか
- `struct file` / `fs_stdioflag` のままで tty を載せるか、最小 `file_ops` を導入するか
- resize 時の reflow をするか
  - 初期段階では「画面再計算と再描画のみ、scrollback の再折り返しなし」が妥当
- unlink / rmdir を ext3 上でどこまで厳密に扱うか
  - 初期段階では「空 directory のみ削除」「開いている file は拒否」で十分か
- `fork` 未実装のまま pipe / redirection をどう通すか
  - file table 継承で行くか、`execve` に stdio remap 引数を追加するか
- `vi` 用の raw 入力 API を POSIX 風 termios 名で出すか、専用 API で出すか
- `vi` の画面切り替えを通常画面の全再描画で済ませるか、alternate screen を追加するか
- 漢字変換と候補 UI を、Plan 14 の後続 plan としてどう分離するか

## タスクリスト

実装用の具体タスクは [TASKS.md](TASKS.md) に分離する。

## 変更対象の中心

- 既存
  - `src/vga.c`
  - `src/include/vga.h`
  - `src/syscall.c`
  - `src/key.c`
  - `src/kernel.c`
  - `src/include/fs.h`
  - `src/process.c`
  - `src/execve.c`
  - `src/usr/init.c`
  - `src/usr/command/eshell.c`
  - `src/usr/command/makefile`
  - `src/usr/makefile`
  - `tests/Makefile`
- 新規候補
  - `src/drivers/bga.c`
  - `src/include/fb.h`
  - `src/include/display_backend.h`
  - `src/display/*.c`
  - `src/tty/*.c`
  - `src/include/tty.h`
  - `src/usr/command/term.c`
  - `src/usr/include/termios.h`
  - `src/usr/include/winsize.h`
  - `src/usr/command/touch.c`
  - `src/usr/command/mkdir.c`
  - `src/usr/command/rm.c`
  - `src/usr/command/rmdir.c`
  - `src/usr/command/mv.c`
  - `src/pipe.c`
  - `src/include/pipe.h`
  - `src/usr/command/vi.c`
  - `src/usr/include/vi.h`
  - `tests/test_vt_parser.c`
  - `tests/test_terminal_surface.c`
  - `tests/test_keymap.c`
  - `tests/test_tty.c`
  - `tests/test_pipe.c`
  - `tests/test_ext3fs_crud.c`
  - `tests/test_vi_buffer.c`
  - `tests/test_utf8.c`
  - `tests/test_wcwidth.c`
  - `tests/test_font_default.c`
  - `tests/test_ime_romaji.c`
  - `src/test/run_qemu_fs_crud_smoke.py`
  - `src/test/run_qemu_shell_io_smoke.py`
  - `src/test/run_qemu_terminal_smoke.py`
  - `src/test/run_qemu_vi_smoke.py`
  - `src/test/run_qemu_utf8_smoke.py`
  - `src/test/run_qemu_ime_smoke.py`
