# Plan 05: TTY / PTY 層

## 概要

client-side terminal を成立させるため、shell と表示を切り離す。
そのために標準入出力の直結をやめ、TTY/PTY ベースへ再設計する。

## 現状

- `sys_write()` は標準出力を直接 `_kputc()` に書く
- `sys_read()` は `get_stdin()` を直接読む
- shell は「コンソールそのもの」と結合している

## 方針

- kernel に最低限の TTY / PTY を持たせる
- terminal client が PTY master、shell が PTY slave を使う
- canonical mode と raw mode を段階導入する

## 実装ステップ

1. `tty` 構造体と `pty master/slave` のバッファ構造を定義する
2. `sys_read/sys_write` を stdio flag ではなく tty file operation 経由へ寄せる
3. echo, backspace, newline を line discipline 側へ寄せる
4. shell 起動時に PTY slave を割り当てる
5. terminal client は PTY master を開き、出力を読み、入力を流し込む
6. 将来の `ioctl(TIOCGWINSZ/TIOCSWINSZ)` に備えた設計にする

## 変更対象

- 既存
  - `src/syscall.c`
  - `src/process.c`
  - `src/usr/command/eshell.c`
- 新規候補
  - `src/tty/tty.c`
  - `src/tty/pty.c`
  - `src/include/tty.h`
  - `src/include/termios.h`

## 完了条件

- shell と terminal client が別プロセスとして動く
- 標準入出力が PTY 越しに流れる
- canonical/raw の導入余地がある

