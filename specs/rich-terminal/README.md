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
- UTF-8 完全対応の初期実装

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

## Plans

| # | ファイル | 概要 | 依存 |
|---|---------|------|------|
| 01 | [01-display-abstraction.md](plans/01-display-abstraction.md) | VGA 直書きを backend 抽象へ切り出す | なし |
| 02 | [02-linear-framebuffer-driver.md](plans/02-linear-framebuffer-driver.md) | QEMU/Bochs 向け線形 framebuffer を使えるようにする | 01 |
| 03 | [03-font-and-cell-renderer.md](plans/03-font-and-cell-renderer.md) | ビットマップフォントと文字セル描画を実装する | 02 |
| 04 | [04-input-event-pipeline.md](plans/04-input-event-pipeline.md) | 文字列キューから key event 方式へ移行する | 01 |
| 05 | [05-tty-pty-layer.md](plans/05-tty-pty-layer.md) | 標準入出力を TTY/PTY ベースに再設計する | 04 |
| 06 | [06-terminal-client-core.md](plans/06-terminal-client-core.md) | userland terminal client を導入する | 03, 04, 05 |
| 07 | [07-vt-parser.md](plans/07-vt-parser.md) | ANSI/VT100 の主要シーケンスを解釈する | 06 |
| 08 | [08-shell-integration-and-resize.md](plans/08-shell-integration-and-resize.md) | eshell 起動、列数伝播、再描画と移行を整える | 05, 06, 07 |
| 09 | [09-testing-and-benchmarks.md](plans/09-testing-and-benchmarks.md) | テスト、QEMU 検証、描画性能計測を整える | 全体横断 |

## 実装順序

1. まず VGA 直書きを抽象化し、表示 backend を差し替え可能にする
2. 次に QEMU/Bochs 向けの線形 framebuffer と文字セルレンダラを作る
3. その上で入力を key event 化し、TTY/PTY を挟んで shell と terminal を分離する
4. terminal client に VT parser、scrollback、resize を載せる
5. 最後に boot path とテストを固める

## 変更対象の中心

- 既存
  - `src/vga.c`
  - `src/include/vga.h`
  - `src/syscall.c`
  - `src/key.c`
  - `src/kernel.c`
  - `src/usr/command/eshell.c`
- 新規候補
  - `src/drivers/bga.c`
  - `src/include/fb.h`
  - `src/tty/*.c`
  - `src/include/tty.h`
  - `src/usr/command/term.c`
  - `src/usr/include/termios.h`
  - `tests/test_vt_parser.c`
  - `tests/test_terminal_surface.c`

