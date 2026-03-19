# Plan 11: shell の pipe / redirection

## 概要

shell で `|`, `>`, `<` を使えるようにし、Plan 10 の file CRUD と組み合わせて
入出力を合成できるようにする。
初期ゴールは `ls > out.txt`, `cat < out.txt`, `ls | cat` が `term` 上で通る状態。

## 依存と出口

- 依存: 08, 09, 10
- この plan の出口
  - `pipe`, `dup` 系の最小 syscall が kernel から userland まで通る
  - `eshell` が `|`, `>`, `<` を解釈して child process の stdio を組める
  - Plan 10 の command 群と組み合わせて shell 上の基本 I/O 合成が成立する

## 方針

- 最初は shell の I/O 合成だけに絞る
- pipeline は 2 コマンド 1 本の `|` から始める
- redirection は `>`, `<` を優先する
  - `>>` は Plan 16、`2>`, `2>&1` は Plan 22、heredoc はさらに後回し
- `fork` 未実装の現状を前提に、child process へ fd を渡す経路を先に固める

## 設計判断

- `pipe()` は匿名 pipe として実装する
  - filesystem 上の node は作らず、`struct file_ops` で read/write を持つ
- `dup()` は shell の fd 保存と復元に使える最小機能から入れる
- `execve()` 系は stdio 継承または明示 remap を扱えるようにする
  - `fork + dup2` 前提にしない
- shell parser は最初は単純な token scan のまま拡張する
  - quoting, glob, 複数段 pipeline は後回し
- `cat` は argv 無しなら stdin を読むようにして、`<` と `|` の受け口にする

## 実装ステップ

1. kernel に pipe endpoint とリングバッファを追加する
2. `sys_pipe`, `sys_dup`, `close` 周辺の fd 管理を実装する
3. child process に file table / stdio map を引き継ぐ `execve` 経路を作る
4. `eshell` の parser を拡張し、`cmd`, `cmd > file`, `cmd < file`, `cmd1 | cmd2` を扱う
5. redirection 用の open flags を整理する
   - `>` は `O_CREAT | O_TRUNC | O_WRONLY`
   - `<` は `O_RDONLY`
6. `cat` を stdin fallback 対応にする
7. shell 上で CRUD と I/O 合成を組み合わせた経路を確認する
   - `ls > out.txt`
   - `cat < out.txt`
   - `ls | cat`
8. host test と QEMU smoke で pipe / redirection を固定する

## 変更対象

- 新規候補
  - `src/pipe.c`
  - `src/include/pipe.h`
  - `src/usr/lib/libc/i386/dup.S`
  - `src/usr/lib/libc/i386/pipe.S`
  - `tests/test_pipe.c`
  - `src/test/run_qemu_shell_io_smoke.py`
- 既存
  - `src/syscall.c`
  - `src/include/fs.h`
  - `src/execve.c`
  - `src/include/execve.h`
  - `src/usr/command/eshell.c`
  - `src/usr/include/eshell.h`
  - `src/usr/command/cat.c`
  - `src/usr/include/stdlib.h`
  - `src/usr/include/sys/syscall.h`
  - `src/include/sys/syscalldef.h`
  - `tests/Makefile`

## 検証

- `ls > out.txt` で file が作られ、`cat out.txt` で内容を確認できる
- `cat < out.txt` が stdin 経由で内容を表示できる
- `ls | cat` が pipe 越しに動く
- redirection 後に shell の標準入出力が壊れない

## 完了条件

- shell 上で `|`, `>`, `<` の最小セットが使える
- pipe と fd 複製の基盤が回帰テストで固定される
- `vi` より前に shell の I/O 合成を試せる状態になる
