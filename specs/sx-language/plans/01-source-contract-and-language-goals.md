# Plan 01: Source 契約と言語目標

## 目的

`sx` を「Sodex 内で agent が書く主言語」として成立させるために、
source file 単位、entrypoint、encoding、字句規則の前提を先に固定する。
ここが曖昧だと、`sxi` の REPL / file 実行と、将来の `sxc` の compile 単位がずれる。

## 背景

調査時点で、Sodex にはすでに `vi`、UTF-8、IME、agent、shell script がある。
一方で、主言語の source 契約を shell に寄せると次が弱い。

- command / word 中心で、構文木より token 列に寄りすぎる
- file が大きくなると `SHELL_STORAGE_SIZE` 制約にぶつかりやすい
- error span が statement / expression 単位で出しにくい
- `sxi` と `sxc` が共通 frontend を持ちにくい

このため `sx` は、editor / REPL / compiler の 3 者で同じ source 契約を持つ必要がある。

## 初期 scope

### 1. source file

- 拡張子は `.sx`
- source encoding は UTF-8
- 改行は `\n` 基準で扱い、`CRLF` は loader で吸収する
- string と comment は UTF-8 を許可する
- v0 の keyword と identifier は ASCII に制限する

ASCII identifier 制限は、IME を否定するためではなく、
最初の lexer / parser / diagnostic を簡潔に保つための制限である。

### 2. comment と空白

- line comment は `//`
- block comment は v0 では必須にしない
- whitespace は token separator としてだけ扱う
- 改行は statement separator ではなく、layout 依存構文は導入しない

### 3. source role

v0 では source を 2 役に分けて考える。

- entry script
  - `sxi file.sx` の対象
  - top-level 実行を許す候補
- module
  - `import` で読み込まれる単位
  - top-level side effect を制限する候補

exact な差分は後続 plan で詰めるが、
source role が違っても同じ lexer / parser を通る前提はここで固定する。

### 4. reserved words

最初に reserved word 候補を language plan 側で押さえる。

- `fn`
- `let`
- `if`
- `else`
- `while`
- `return`
- `true`
- `false`
- `import`

`for`、`break`、`continue`、`nil` などは v0 grammar に入れるかを
後続 plan で決める。

## 非ゴール

- source compatibility を C や Rust に寄せること
- shebang、shell-style heredoc、layout rule を最初から入れること
- Unicode identifier や正規化規則を v0 完了条件にすること

## 設計方針

### 1. file 実行と REPL 入力で同じ lexical rule を使う

`sxi -e`、REPL、file 実行で lexer が分かれると挙動がぶれる。
front-end では常に同じ tokenization を通し、REPL 側は incomplete input 判定だけを追加する。

### 2. source は人間と agent の両方が短く編集しやすい形にする

`write_file` 上限があるので、巨大 1 file より
小さい module を並べる運用が自然になる。
source contract は、この分割を邪魔しない必要がある。

### 3. source role と module search path は初期から意識する

script しか考えないと、後で multi-file 化したときに import 契約が崩れる。
逆に module だけを先に厳密化しすぎると、初期の REPL / script UX を損ねる。
そのため `entry script` と `module` の 2 role を最初から切り出す。

## 実装ステップ

1. `.sx` source contract を文書化する
2. keyword / identifier / literal の lexical rule を決める
3. representative corpus / negative corpus を用意する
4. `sxi` と `sxc` が共有する source role 前提を fix する

## 変更対象

- 新規候補
  - `src/usr/include/sx_lexer.h`
  - `src/usr/include/sx_source.h`
  - `src/usr/lib/libsx/lexer.c`
  - `tests/test_sx_lexer.c`
  - `tests/fixtures/sx/valid/`
  - `tests/fixtures/sx/invalid/`
- 文書
  - `specs/sx-language/README.md`
  - `specs/sxi-runtime/README.md`

## 検証

- valid fixture
  - UTF-8 comment / string を含む source
  - ASCII identifier と reserved word の境界
  - single-file script
- invalid fixture
  - 不正 escape
  - keyword typo
  - closed していない string
  - source role に違反する top-level 構文

## 完了条件

- `.sx` file の source 契約が 1 つの文書で説明できる
- lexer fixture が valid / invalid を安定して分ける
- `sxi` と `sxc` が同じ source role 前提を共有できる
- keyword と identifier の境界が実装前に固定される
