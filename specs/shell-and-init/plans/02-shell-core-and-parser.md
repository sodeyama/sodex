# Plan 02: Shell Core と Parser

## 概要

interactive `eshell` と non-interactive `sh` が共通利用する
shell core を作る。

最初に必要なのは「1 行 parser の延長」ではなく、
script を順に実行できる AST と executor である。

## MVP 範囲

- simple command
- sequential list
  - `;`
  - `<newline>`
- AND-OR list
  - `&&`
  - `||`
- background
  - `&`
- pipeline
  - `|`
- redirection
  - `<`, `>`, `>>`
- comments
  - `#`
- quote / escape
  - `'...'`, `"..."`, `\`

## 方針

- parser は token 列ではなく AST まで落とす
- `eshell` は prompt / line editor / history を持つ frontend に寄せる
- `sh` は file reader / `-c` reader を持つ frontend に寄せる
- shell 実行状態は `struct shell_state` のような 1 か所に集約する

## 初期にやらないもの

- shell function
- arithmetic expansion
- command substitution
- full job control
- here-document
- brace expansion

これらは generic shell として有用だが、
`/etc/init.d/rcS` と service script を先に成立させるには後回しでよい。

## 設計判断

- `eshell_parser.c` の inplace token parser は置き換えまたは内部限定にする
- line 単位ではなく file 全体を `<newline>` を保ったまま parse する
- parse error は interactive/non-interactive で扱いを分ける
  - interactive: error を表示して次へ
  - non-interactive: script abort を基本にする

## 変更対象

- 既存
  - `src/usr/command/eshell.c`
  - `src/usr/lib/libc/eshell_parser.c`
  - `src/usr/include/eshell_parser.h`
- 新規候補
  - `src/usr/command/sh.c`
  - `src/usr/lib/libc/shell_lexer.c`
  - `src/usr/lib/libc/shell_parser.c`
  - `src/usr/lib/libc/shell_executor.c`
  - `src/usr/include/shell.h`

## 検証

- host test で AST 生成を確認できる
- `sh -c 'a && b || c'` の結合規則を固定できる
- `<newline>` 区切りの script を順に実行できる

## 完了条件

- `eshell` と `sh` が共通 parser/executor を使える
- `;`, `<newline>`, `&&`, `||`, `&`, `|` を含む script を parse できる
- interactive と non-interactive の差分が frontend に閉じる
