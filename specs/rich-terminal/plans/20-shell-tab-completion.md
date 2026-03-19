# Plan 20: shell のタブ補完

## 概要

現在の shell は quoting、複数段 pipeline、`>>` までは扱えるが、
対話入力はまだ最小で、`cat hoge_` のような途中入力に対して
Tab で候補を補完したり、複数候補から選んだりはできない。

現状の構造では `eshell` 自体は Enter まで 1 行を受け取れず、
途中入力の補完は shell 単体では完結しない。
一方で `term` は raw key event、IME overlay、PTY 書き込み、
termios / foreground pid の問い合わせをすでに持っている。

この plan では初期実装のタブ補完を `term` 側へ置き、
shell prompt 上の canonical 入力に対してだけ
path 補完と候補巡回を提供する。
最初のゴールは `cat hoge_` で Tab を押すと候補が絞られ、
一意ならその場で補完され、複数候補なら Tab / Shift+Tab で巡回できる状態。

## 依存と出口

- 依存: 16, 17
- この plan の出口
  - `term` が shell prompt 用の入力行 state を保持できる
  - shell が foreground かつ `ICANON` の間だけ、Tab completion が有効になる
  - `cat hoge_` のような path prefix に対して候補を列挙し、
    一意補完、共通 prefix 延長、候補巡回ができる
  - 候補 UI を overlay で表示でき、IME overlay と破綻なく共存できる
  - host test と QEMU smoke で補完導線の回帰を検知できる

## 現状

- `eshell` は `read(STDIN_FILENO, ...)` で Enter 済み 1 行だけを受け取る
- `tty` の canonical 編集は append / Backspace / Enter だけで、
  shell 文脈を見た補完 hook はない
- `term` は raw key を PTY へ流す前に変換でき、IME の候補 UI も持つ
- guest userland には汎用 `dirent` API はないが、
  raw ext3 directory entry を読んで列挙する実装はすでに存在する
- `vi` など raw mode の全画面 app まで Tab を奪うと壊れるため、
  shell prompt にだけ適用する条件付けが必要である

## 方針

- 初期実装の補完責務は `term` に置く
  - `eshell` と `tty` はまず大きく崩さない
- 補完の active 条件を厳しくする
  - shell pid が foreground
  - `ICANON` が有効
  - IME の preedit / conversion が active ではない
- まずは行末補完だけを成立させる
  - cursor 左右移動や途中挿入は非ゴール
- 候補列挙は pure logic helper と path 列挙 helper に分ける
  - shell token の切り出し
  - dir prefix と basename prefix の分離
  - 候補の共通 prefix 計算
- 候補 UI は `term` overlay を再利用する
  - 一意補完時は inline 更新だけ
  - 複数候補時は overlay で件数と現在候補を見せる

## 非ゴール

- readline 相当の全面的な line editor
- 履歴検索、左右カーソル移動、途中挿入、kill/yank
- glob 展開や fuzzy match
- shell parser 全体の再設計
- `vi` や raw app の key binding 拡張

## 設計判断

- `tty` canonical へ shell 専用補完を直接入れない
  - file system 文脈と shell token 文脈を `tty` に持ち込まない
- `term` は shell 入力の shadow state を別持ちする
  - local key で送った bytes を追跡し、Tab 時の置換に使う
  - PTY からの予期しない出力を見たら state を無効化し、無理に同期しない
- 補完対象は「現在行の最後の shell word」に限定する
  - space
  - quote
  - backslash escape
  - `|`, `<`, `>`, `>>`, `&&`, `||`, `;`, `&`
  を区切りとして扱う
- path 補完は component 単位で行う
  - `foo/bar`
  - `./foo`
  - `../foo`
  - `/tmp/foo`
  - 日本語を含む UTF-8 filename
- 複数候補時の操作は最小にする
  - `Tab` で次候補
  - `Shift+Tab` で前候補
  - `Esc` で補完 state を破棄
- inline 反映は backspace + 追加入力で行う
  - 一意な directory は末尾に `/`
  - 一意な file は末尾に space を付ける
- completion overlay と IME overlay は同じ表示枠を共有する
  - IME active 中は completion を始めない
  - completion active 中は completion overlay を優先する

## 実装ステップ

1. `term` に shell 補完専用の line state を追加し、
   shell pid / foreground pid / termios から active 条件を判定できるようにする
2. raw ext3 directory entry を読む処理を再利用可能な helper へ切り出し、
   path prefix から候補列を返す `shell_completion` 層を追加する
3. 現在行末の shell word を quote / escape / redirection aware に切り出す helper を追加する
4. 共通 prefix 計算、一意補完、候補巡回、cancel を pure logic として分離する
5. `term` の key 変換に Tab / Shift+Tab / Esc の completion action を追加し、
   inline 置換を PTY へ反映する
6. `term` overlay に completion 候補 UI を追加し、件数と選択中候補を表示する
7. host test で token 切り出し、path match、候補巡回、cancel/reapply を固定する
8. QEMU smoke で `cat hoge_` の補完導線と複数候補巡回を固定する

## 変更対象

- 既存
  - `src/usr/command/term.c`
  - `src/usr/command/eshell.c`
  - `src/usr/include/key.h`
  - `src/usr/include/termios.h`
  - `src/usr/lib/libagent/tool_list_dir.c`
  - `src/test/run_qemu_shell_io_smoke.py`
  - `tests/Makefile`
- 新規候補
  - `src/usr/lib/libc/shell_completion.c`
  - `src/usr/include/shell_completion.h`
  - `tests/test_shell_completion.c`
  - `src/test/run_qemu_shell_completion_smoke.py`
  - `src/test/data/term_completion_overlay_reference.json`

## 検証

- prompt で `cat hoge_` + `Tab` により一意候補があればその場で補完される
- `dir_` + `Tab` で directory が補完された場合は `/` が付く
- 候補が複数あるとき、`Tab` / `Shift+Tab` で巡回できる
- `Esc` で補完 state を破棄し、元の prefix に戻せる
- 日本語 filename の prefix でも UTF-8 を壊さず補完できる
- `vi` 起動中や IME conversion 中には補完が誤発火しない
- QEMU 上でも screenshot / 実ファイル操作の両方で回帰を検知できる

## 完了条件

- shell prompt 上で、最低限実用になる path tab completion が使える
- 補完の責務が `term` 側に局所化され、既存 shell / tty を大きく壊さない
- host / QEMU の両方で、補完導線を継続的に固定できる
