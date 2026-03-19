# Plan 22: shell の stderr redirection と pager

## 概要

Plan 11 と Plan 16 で、shell の最小 pipe / redirection と quoting、複数段 pipeline、`>>` までは成立した。
ただし日常利用ではまだ次の不足が残っている。

- `command > output.log 2>&1` のような fd 指定 redirection が使えない
- stdout と stderr を別 file に分けたり、片方だけを捨てたりする制御が弱い
- 長い出力を file / pipe 経由で読む `more` / `less` 相当の pager command がない
- shell / command の診断が stdout 側へ混ざる箇所があり、stderr 分離の効果が薄い

この plan では、shell の I/O 合成を「最小 redirection」から「stdout / stderr を意識して運用できる状態」へ広げ、
その上で `more` / `less` 相当の pager command を追加する。

## 依存と出口

- 依存: 11, 13, 16
- この plan の出口
  - `eshell` が `2>`, `2>>`, `2>&1`, `1>&2` を含む fd 指定 redirection を解釈できる
  - redirection は左から右へ評価され、`cmd > out 2>&1` と `cmd 2>&1 > out` の差を保持できる
  - shell 本体と代表 command 群の診断が stderr へ寄る
  - `more` と最小 `less` が file / stdin / pipe 入力を page 単位で閲覧できる
  - host test と QEMU smoke で advanced redirection と pager 導線を回帰検知できる

## 現状

- `shell_command` は `input_path`, `output_path`, `append_output` の固定 field しか持たず、
  fd 番号付き redirection や dup の順序を表現できない
- shell executor は stdin / stdout の差し替えを中心に作られており、
  stderr の複製・保存・復元を command 単位で一般化していない
- `>>` は通るが、`2>`, `2>>`, `2>&1`, `1>&2` のような stream 制御は未対応
- 長文出力は terminal scrollback で眺められるものの、`cmd | more` や `less file.log` のような pager workflow は存在しない
- command によってはエラー文言が stdout に出ており、stderr redirection を入れても期待通りに分離できない

## 方針

- まず shell の redirection model を一般化し、その上で pager command を追加する
- redirection の評価順は POSIX に寄せ、左から右へ適用する
  - `cmd > out 2>&1` は stdout を file に向けた後で stderr を stdout へ束ねる
  - `cmd 2>&1 > out` は stderr を元の stdout へ束ねた後で stdout だけ file に向ける
- pager は file だけでなく pipe 入力を第一級に扱う
- 初期 pager は実用上よく使う subset に絞る
  - `more`: `Space`, `Enter`, `b`, `q`
  - `less`: `Space`, `b`, `g`, `G`, `/`, `n`, `N`, `q`
- UTF-8 / wide char 前提で表示幅を壊さない

## 設計判断

- `shell_command` の固定 `input_path` / `output_path` から、
  順序付き `redirection action` 配列へ移行する
  - open-read
  - open-write-trunc
  - open-write-append
  - dup-fd
  - 必要なら close-fd を後段に拡張できる形にする
- parser は `2>`, `2>>`, `2>&1`, `1>&2` を token として認識し、
  実行時は action を左から右へ適用する
- path operand の展開は既存 shell expansion の流れに合わせる
  - fd 番号そのものは展開しない
  - file path だけ quote / variable 展開の対象にする
- builtin 実行と外部 command 実行で同じ redirection helper を使う
  - builtin だけ別挙動にしない
- pager 本体は reusable な core を切り出し、`more` と `less` の entry point から共有する
- pager は既存 terminal 機能を前提にし、独自 window system や mouse 依存は持ち込まない
- 初期実装では書式色付け、複数 window、follow mode は非ゴールにする

## 実装ステップ

1. `shell_command` を順序付き redirection action 配列へ拡張する
2. tokenizer / parser を更新し、`2>`, `2>>`, `2>&1`, `1>&2` を解釈できるようにする
3. shell executor に fd remap / dup / restore helper を追加し、left-to-right semantics を通す
4. pipeline / builtin / background 実行で advanced redirection が破綻しないように整える
5. shell 本体と代表 command 群の診断を stderr へ寄せる
   - `eshell`
   - file command 群
   - network / utility command の主要エラー経路
6. pager core を追加し、line wrapping、page 単位移動、prompt 表示、stdin/file source 切り替えを実装する
7. `/usr/bin/more` を追加し、pipe / file の長文を page 単位で読めるようにする
8. `/usr/bin/less` の最小 subset を追加し、後退・先頭/末尾移動・検索の基本操作を通す
9. host test と QEMU smoke を追加し、advanced redirection と pager 導線を固定する

## 変更対象

- 既存
  - `src/usr/include/shell.h`
  - `src/usr/lib/libc/shell_parser.c`
  - `src/usr/lib/libc/shell_executor.c`
  - `src/usr/command/eshell.c`
  - `src/usr/command/makefile`
  - `src/usr/makefile`
  - `src/test/run_qemu_shell_io_smoke.py`
  - `tests/Makefile`
- 新規候補
  - `src/usr/include/pager.h`
  - `src/usr/lib/libc/pager.c`
  - `src/usr/command/more.c`
  - `src/usr/command/less.c`
  - `tests/test_shell_redirection.c`
  - `tests/test_pager.c`
  - `src/test/run_qemu_pager_smoke.py`

## 検証

- `cmd > out.log 2>&1` で stdout / stderr の両方が `out.log` に入る
- `cmd 2> err.log` で stderr だけが `err.log` に入り、stdout は terminal に残る
- `cmd > out.log 2> err.log` で 2 本の file に分離できる
- `cmd 2>&1 | more` が pipe 越しの merged output を page 単位で読める
- `more file.log` が page 送りと終了を扱える
- `less file.log` が前後移動と検索の最小 subset を扱える
- UTF-8 を含む log / text を pager で読んでも文字幅が破綻しない
- redirection 後に shell の標準入出力状態が壊れない

## 完了条件

- shell 上で stdout / stderr を意識した redirection が日常利用レベルで使える
- `more` / `less` 相当の pager workflow が guest 内で成立する
- long output の閲覧と log 保存が shell 単体で閉じる
