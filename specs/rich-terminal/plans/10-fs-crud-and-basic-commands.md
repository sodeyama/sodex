# Plan 10: ファイル/フォルダ CRUD と基本コマンド

## 概要

shell 上で最低限のファイル操作を自走できるようにするため、
ファイル・フォルダの CRUD を成立させる syscall と userland command を整備する。
初期ゴールは `touch`, `mkdir`, `rm`, `rmdir`, `mv` が `term` 上で使え、
`vi` 導入前にファイル作成・削除・移動の基本導線が揃っている状態。
pipe / redirection は Plan 11 に分離する。

## 依存と出口

- 依存: 08, 09
- この plan の出口
  - `touch`, `mkdir`, `rm`, `rmdir`, `mv` が build と image に入る
  - `unlink`, `rmdir`, `rename` 系 syscall が kernel から userland まで通る
  - 既存の `ls`, `cat`, `cd`, `pwd` と合わせて最小 CRUD が shell 上で成立する

## 方針

- まずは「単純で壊れにくい namespace 操作」を優先する
- `touch` は専用 syscall を増やさず `open(..., O_CREAT)` + `close()` で成立させる
- 削除系は file と directory を分ける
  - `rm` は `unlink`
  - `rmdir` は空 directory のみ削除
- permission, owner, timestamp の厳密互換は後回しにする

## 設計判断

- path 解決は既存 `open_env()` / `ext3_*` の流儀に合わせる
  - 相対 path と `/usr/bin` 探索の責務を混ぜない
- syscall 番号は既存定義を活かす
  - `SYS_CALL_UNLINK`, `SYS_CALL_RENAME`, `SYS_CALL_MKDIR`, `SYS_CALL_RMDIR` を実装側へ接続する
- ext3 側は namespace 変更 helper を分離する
  - dentry 走査、親 directory 更新、inode/link count 更新を大きな関数に埋め込まない
- 削除の初期実装は「開いている fd が無い」「空 directory のみ」という単純条件でよい
  - POSIX 完全互換よりも破損しないことを優先する
- `mv` はまず同一 filesystem 内 rename に限定する
  - cross-device move や copy+unlink fallback は後回し

## 実装ステップ

1. ext3 の namespace 更新に必要な内部 helper を整理する
   - 親 dentry 探索
   - 対象 entry の検索
   - directory entry の追加/削除
   - inode / block bitmap 更新
2. `ext3_unlink`, `ext3_rmdir`, `ext3_rename` の最小実装を追加する
3. `syscall.c` に `unlink`, `mkdir`, `rmdir`, `rename` の dispatcher を追加する
4. userland libc に `unlink()`, `rmdir()`, `rename()` wrapper と header 宣言を追加する
5. `/usr/bin/touch`, `/usr/bin/mkdir`, `/usr/bin/rm`, `/usr/bin/rmdir`, `/usr/bin/mv` を追加する
6. 既存 command と合わせた shell 操作を確認する
   - `mkdir dir`
   - `touch dir/a.txt`
   - `mv dir/a.txt dir/b.txt`
   - `rm dir/b.txt`
   - `rmdir dir`
7. host test と QEMU smoke で CRUD の基本フローを固定する

## 変更対象

- 新規候補
  - `src/usr/command/touch.c`
  - `src/usr/command/mkdir.c`
  - `src/usr/command/rm.c`
  - `src/usr/command/rmdir.c`
  - `src/usr/command/mv.c`
  - `src/usr/lib/libc/i386/unlink.S`
  - `src/usr/lib/libc/i386/rmdir.S`
  - `src/usr/lib/libc/i386/rename.S`
  - `tests/test_ext3fs_crud.c`
  - `src/test/run_qemu_fs_crud_smoke.py`
- 既存
  - `src/ext3fs.c`
  - `src/include/ext3fs.h`
  - `src/syscall.c`
  - `src/include/sys/syscalldef.h`
  - `src/usr/include/sys/syscall.h`
  - `src/usr/include/stdlib.h`
  - `src/usr/command/makefile`
  - `src/usr/makefile`
  - `tests/Makefile`

## 検証

- `mkdir work` 後に `ls` で directory が見える
- `touch work/memo.txt` 後に `cat work/memo.txt` が空ファイルとして開ける
- `mv work/memo.txt work/note.txt` 後に旧 path が消え、新 path が見える
- `rm work/note.txt` 後に再度開けない
- `rmdir work` が空 directory でだけ成功する

## 完了条件

- shell 上でファイル・フォルダの最小 CRUD が成立する
- mutating syscall が kernel から userland command まで一貫して通る
- `vi` 導入前提のファイル操作基盤と回帰テストが揃う
