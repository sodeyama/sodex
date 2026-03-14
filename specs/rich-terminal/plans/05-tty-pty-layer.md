# Plan 05: TTY / PTY 層

## 概要

client-side terminal を成立させるため、shell と表示を切り離す。
そのために標準入出力の直結をやめ、TTY/PTY ベースへ再設計する。

## 依存と出口

- 依存: 04
- この plan の出口
  - shell が PTY slave 側で動く
  - terminal client が PTY master 側で入出力を扱える
  - `sys_read()` / `sys_write()` が stdio flag 直結から離れる

## 現状

- `sys_write()` は標準出力を直接 `_kputc()` に書く
- `sys_read()` は `get_stdin()` を直接読む
- shell は「コンソールそのもの」と結合している

## 方針

- kernel に最低限の TTY / PTY を持たせる
- terminal client が PTY master、shell が PTY slave を使う
- canonical mode と raw mode を段階導入する

## 設計判断

- `struct file` 周辺に最小限の `file_ops` を入れる方向を優先する
  - stdio だけ特別扱いし続けると PTY 導入後の拡張が苦しい
- 最初は controlling TTY を 1 個だけ持てればよい
- termios は subset 実装で始める
  - `ICANON`, `ECHO`, `ISIG` と `VINTR`, `VERASE`, `VKILL` 程度を先に扱う
- PTY は paired ring buffer でよい
  - job control や複雑な session 管理は後回し

## 実装ステップ

1. `tty`, `pty_master`, `pty_slave`, `line_discipline` の構造体を定義する
2. `struct file` と `sys_read/sys_write` を tty file operation 経由に寄せる
3. echo, backspace, newline, erase を line discipline 側へ移す
4. canonical mode と raw mode の最小切り替えを実装する
5. process / execve 側に stdio 用 tty 割り当て経路を作る
6. terminal client が PTY master を開ける API を定義する
7. 将来の `TIOCGWINSZ/TIOCSWINSZ` を想定した winsize 保持欄を入れる

## 変更対象

- 既存
  - `src/syscall.c`
  - `src/include/fs.h`
  - `src/fs.c`
  - `src/process.c`
  - `src/execve.c`
  - `src/usr/command/eshell.c`
- 新規候補
  - `src/tty/tty.c`
  - `src/tty/pty.c`
  - `src/include/tty.h`
  - `src/include/termios.h`

## 検証

- shell と terminal client が別プロセスで動く
- PTY 越しに echo と行編集が成立する
- raw mode で矢印キーなどをそのまま流せる

## 完了条件

- shell と terminal client が別プロセスとして動く
- 標準入出力が PTY 越しに流れる
- canonical/raw の導入余地がある
