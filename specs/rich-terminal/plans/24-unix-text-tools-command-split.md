# Plan 24: Unix text command 実装の command 側分割

## 概要

Plan 23 で `find`, `sort`, `uniq`, `wc`, `head`, `tail`, `grep`, `sed`, `awk`, `cut`, `tr`, `diff`, `tee` は guest / agent から使えるようになった。
ただし実装は `src/usr/lib/libc/unix_text_tools.c` に集中しており、`src/usr/command/*.c` は薄い wrapper だけになっている。

この状態だと command ごとの責務が追いづらく、修正時の影響範囲も見通しにくい。
Plan 24 では、各 command の本体を `src/usr/command/*.c` へ戻し、複数 command で共有する最小 helper だけを `libc` 側へ残す。

## 依存と出口

- 依存: 23
- この plan の出口
  - `src/usr/command/find.c`, `sort.c`, `uniq.c`, `wc.c`, `head.c`, `tail.c`, `grep.c`, `sed.c`, `awk.c`, `cut.c`, `tr.c`, `diff.c`, `tee.c` が各 command の `unix_*_main()` 本体を持つ
  - `src/usr/lib/libc/` には line 読み込み、文字列 builder、range parse、regex subset などの共通 helper だけが残る
  - `tests/test_unix_text_tools` は monolith 1 個ではなく、command object 群 + 共通 helper object 群を直接 link して通る
  - userland build と host test、必要な QEMU smoke が通る

## 方針

- public entrypoint 名は維持する
  - `unix_sort_main()` などの関数名は変えず、host test 側の呼び出し面を壊さない
- `main()` は command 側へ戻す
  - `TEST_BUILD` 時だけ `main()` を外し、同じ source を host test object にも使う
- `libc` に置くのは共有 helper のみ
  - 文字列 builder
  - fd/path 全読み
  - line split と `loaded_text`
  - wildcard / basic regex / substring
  - range list parse
  - UTF-8 文字境界の最小 helper
- command 固有 helper は command 側へ閉じる
  - `find` の tree walk
  - `sort` の key compare
  - `grep` の option / pattern 管理
  - `tr` の set 展開
  - `sed` / `awk` の parser / engine
  - `diff` の表示生成

## 実装ステップ

1. `src/usr/include/unix_text_tool_lib.h` と `src/usr/lib/libc/unix_text_tool_lib.c` を追加し、共有 helper の境界を決める
2. `find` から `tee` までの command で、wrapper を `unix_*_main()` 本体へ置き換える
3. `sed` と `awk` の parser / engine を command 側へ移す
4. `tests/Makefile` を更新し、`test_unix_text_tools` が各 command object を直接 link するようにする
5. `src/usr/lib/libc/unix_text_tools.c` を削除する
6. `make -C tests test_unix_text_tools`、`make -C src test-qemu-unix-text-tools`、`make test` で回帰を確認する

## 変更対象

- 既存
  - `src/usr/command/find.c`
  - `src/usr/command/sort.c`
  - `src/usr/command/uniq.c`
  - `src/usr/command/wc.c`
  - `src/usr/command/head.c`
  - `src/usr/command/tail.c`
  - `src/usr/command/grep.c`
  - `src/usr/command/sed.c`
  - `src/usr/command/awk.c`
  - `src/usr/command/cut.c`
  - `src/usr/command/tr.c`
  - `src/usr/command/diff.c`
  - `src/usr/command/tee.c`
  - `tests/Makefile`
  - `tests/test_unix_text_tools.c`
- 新規
  - `src/usr/include/unix_text_tool_lib.h`
  - `src/usr/lib/libc/unix_text_tool_lib.c`

## 検証

- `make -C tests test_unix_text_tools`
- `make -C src test-qemu-unix-text-tools`
- `make test`

## 完了条件

- unix text command 群の責務が command 単位で追える
- monolith な `unix_text_tools.c` が消える
- host/QEMU の既存回帰検知導線を維持したまま分割が完了する
