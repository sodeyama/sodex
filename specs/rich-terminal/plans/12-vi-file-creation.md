# Plan 12: vi によるファイル作成

## 概要

新しい terminal client 上でフルスクリーン editor `vi` を動かし、
新規ファイルを作成して ext3 に保存できるようにする。
初期ゴールは `vi memo.txt` を開き、入力して `:wq` で保存できる状態。

## 依存と出口

- 依存: 07, 08, 09, 10, 11
- この plan の出口
  - `/usr/bin/vi` が build と image に入る
  - `term` 上で `vi <新規ファイル名>` を起動し、1 文字ずつ編集できる
  - `:w`, `:q`, `:wq` で新規作成と上書き保存ができる

## 方針

- 最初は in-tree の最小 `vi` を作る
- `ncurses` 互換層や外部移植は持ち込まない
- 画面更新は既存 terminal の VT subset を前提にした全画面再描画から始める
- 保存処理は `O_CREAT | O_TRUNC | O_WRONLY` の逐次書き出しで成立させる

## 設計判断

- 初期モードは `normal`, `insert`, `command-line` の 3 つに絞る
- 文字コードは ASCII 前提にする
  - UTF-8 や全角幅計算は後回し
- editor buffer は「行配列 + カーソル位置 + dirty flag」の単純構造で始める
- raw 入力は PTY 単位で切り替える
  - terminal 全体の global input mode ではなく、`ICANON`/`ECHO` の最小 termios API を優先する
- 画面切り替えは最初から alternate screen を必須にしない
  - まずは通常画面の再描画で成立させ、必要なら後で `?1049h/l` を足す
- 保存は常に全内容を書き直す
  - ブロック単位の差分書き換えや undo ログは初期段階で扱わない

## 実装ステップ

1. userland から TTY の `ICANON` / `ECHO` を切り替える最小 API を追加する
2. `vi` が使うキー入力を整理する
   - `ESC`, 矢印, `hjkl`, `i`, `:`, `Backspace`, `Enter`
3. `vi` 用の editor buffer と全画面再描画ループを実装する
4. `normal` / `insert` / `command-line` の状態遷移を実装する
5. 起動時に既存ファイルを読み込むか、新規空バッファを作る
6. `:w`, `:q`, `:wq` を実装し、Plan 10/11 の file / stdio 基盤の上で保存する
7. `eshell` から `vi` を起動できる build/image 導線を追加する
8. host test と QEMU smoke test で「新規作成して保存」の流れを固定する

## 変更対象

- 新規候補
  - `src/usr/command/vi.c`
  - `src/usr/include/termios.h`
  - `src/usr/include/vi.h`
  - `src/usr/lib/libc/vi_buffer.c`
  - `src/usr/lib/libc/vi_screen.c`
  - `tests/test_vi_buffer.c`
  - `src/test/run_qemu_vi_smoke.py`
- 既存
  - `src/usr/command/makefile`
  - `src/usr/makefile`
  - `src/usr/include/tty.h`
  - `src/syscall.c`
  - `src/tty/tty.c`

## 検証

- `term` 上で `vi memo.txt` を起動し、ステータス行つきで全画面表示できる
- insert mode で入力した内容が画面に反映される
- `:wq` 後に `cat memo.txt` で保存内容を確認できる
- 既存ファイルを開いた場合に内容を読み込める

## 完了条件

- `vi <path>` が `term` 上で実用的に起動する
- 新規ファイルを作成し、保存して shell へ戻れる
- QEMU 上でファイル作成フローの回帰検知ができる
