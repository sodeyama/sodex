# Plan 08: Shell 使い勝手と展開機能

## 概要

Plan 07 までで `sh` / `eshell` は、
script 実行、job control の最小形、`if` / loop、最小 `test` まで揃った。
一方で、日常的な shell として見ると次がまだ不足している。

- `alias`, `unalias`
- command lookup を確認する `type`, `command -v`
- interactive history と再実行
- `~` 展開
- pathname glob (`*`, `?`) 展開

この phase では、shell を「書ける」だけでなく
「普段使いしやすい」方向へ進める。
completion は既に `term` + `shell_completion.c` にあるため、
次段は alias / history / expansion を中心に固める。

2026-03-22 に本 plan を実装し、`alias` / `unalias`,
`type` / `command -v`, current session history, `!!`, `!prefix`,
`~`, basic glob (`*`, `?`) を `sh` / `eshell` 共通 core に追加した。
検証は host unit test `test_shell_usability` と
QEMU smoke `test-qemu-shell-usability` で固定した。

## 調査メモ

2026-03-22 時点の実装確認では、次が見えている。

- builtin は `cd`, `exit`, `export`, `set`, `.`, `wait`, `jobs`, `fg`, `bg`,
  `trap`, `break`, `continue`, `echo`, `true`, `false`, `test`, `[` に限られる
- `alias`, `unalias`, `history`, `type`, `command -v`, `hash` は未実装
- `term` と `shell_completion.c` には prompt 追跡と token 単位の completion がある
- `find` / `unix_text_tool_lib` には wildcard 処理があるが、shell 自身は glob 展開しない
- parser / expander は quote を保った token 化はできるが、
  alias 展開や history 展開の入口はまだ無い

つまり、対話補助の土台は一部あるが、
lookup と expansion の shell 本体側が足りない。

## 初期 scope

### 1. alias

- `alias name='value'`
- `alias`
- `alias name`
- `unalias name`

初期実装は command position の先頭 word のみを対象にする。
予約語や assignment word への複雑な再展開は後段へ回してよい。

### 2. command lookup

- `type name`
- `command -v name`

少なくとも次を区別できるようにする。

- alias
- builtin
- `/usr/bin/*` の external command
- not found

### 3. history

- current shell 内の履歴保持
- `history` builtin
- 直前再実行の最小形
  - 例: `!!`
- prefix 指定の最小形
  - 例: `!ec`

永続化は後段でもよい。
まずは current shell session のみで成立させる。

### 4. expansion

- `~` -> `$HOME`
- glob
  - `*`
  - `?`

quote された token では展開しない。
match 0 件時は literal 維持を基本にする。

## 非ゴール

- shell function
- `hash -r`
- `fc`
- command substitution `` `...` `` / `$(...)`
- arithmetic expansion
- brace expansion
- here-document / here-string
- history の永続化
- shell option 群 (`set -e`, `set -u`, `noglob` など)

## 設計方針

### 1. command 解決経路を 1 箇所へ寄せる

alias, builtin, external command の判定が
executor と `type` / `command -v` で分かれると崩れやすい。

そのため command resolution helper を作り、
少なくとも次を shared にする。

- alias lookup
- builtin lookup
- external path lookup
- not found 判定

### 2. alias は parser ではなく実行前の command-position で展開する

AST や tokenizer に alias 展開を混ぜすぎると、
quote や継続入力の扱いが壊れやすい。

初期実装では、
simple command の先頭 word を確定した後で alias 展開する。
再帰 alias は上限付きで止める。

### 3. history は interactive 寄りだが、実行前展開として扱う

`!!` や `!prefix` は parser とは別の前処理として扱う。
これにより `sh file` の script 実行に history を混ぜず、
`eshell` / interactive `sh` のみへ適用しやすくする。

### 4. glob / `~` は word expansion 層へ足す

glob は parser の責務ではなく expander の責務に寄せる。
これにより quote の有無や token logical text を見ながら、
completion と似た token 単位の扱いに揃えやすい。

### 5. completion と競合しない形にする

`term` には既に completion 状態機械がある。
alias / glob / history を足しても、prompt 認識や token completion が
壊れないよう、completion 側が見ている line 表現を維持する。

## 実装ステップ

1. alias table と command resolution helper を追加する
2. `alias` / `unalias` / `type` / `command -v` を実装する
3. interactive history buffer と `history` builtin を追加する
4. `!!` / `!prefix` の最小展開を入れる
5. `~` 展開と glob 展開を expander に追加する
6. host test / QEMU smoke / docs を更新する

## 変更対象

- 既存
  - `src/usr/command/eshell.c`
  - `src/usr/command/sh.c`
  - `src/usr/include/shell.h`
  - `src/usr/lib/libc/shell_executor.c`
  - `src/usr/lib/libc/shell_parser.c`
  - `src/usr/lib/libc/shell_script.c`
  - `src/usr/lib/libc/shell_vars.c`
  - `src/usr/lib/libc/shell_completion.c`
  - `src/usr/lib/libc/unix_text_tool_lib.c`
  - `tests/test_shell_core.c`
  - `tests/test_shell_programmable.c`
- 新規候補
  - `src/usr/lib/libc/shell_alias.c`
  - `src/usr/lib/libc/shell_history.c`
  - `src/usr/lib/libc/shell_expand.c`
  - `tests/test_shell_usability.c`
  - `src/test/run_qemu_shell_usability_smoke.py`

## 検証

- host alias test
  - `alias ll='ls'`
  - recursive alias guard
  - `unalias`
- host lookup test
  - `type cd`
  - `type test`
  - `command -v ls`
  - not found
- host history test
  - `history`
  - `!!`
  - `!prefix`
- host expansion test
  - `~`
  - `*.txt`
  - no-match literal 維持
  - quote で展開しない
- QEMU smoke
  - redirected `eshell` で alias と history を実行できる
  - `sh -c` または boot-time script で `~` / glob を確認できる

## 完了条件

- `alias` / `unalias` が current shell 状態で動く
- `type` / `command -v` で alias / builtin / external を見分けられる
- interactive shell で `history`, `!!`, `!prefix` が使える
- `~` と basic glob が quote を壊さず使える
- completion と競合せず、host test と QEMU smoke が green になる

## 実装後メモ

- alias table / history buffer は `struct shell_state` に持たせた
- alias / builtin / external の lookup は shared helper に寄せた
- external command 解決は `PATH` を見て `/usr/bin/*` へ落とす
- history 展開は interactive `sh` / `eshell` の実行前処理に限定した
- glob は guest 側では ext3 directory entry を直接走査し、host test は `dirent` ベースで回した
- completion 側の state machine は変更せず、shell 本体の expansion のみ追加した
- 未対応は当初どおり `hash`, shell function, 永続 history, command substitution,
  arithmetic expansion, brace expansion を残す
