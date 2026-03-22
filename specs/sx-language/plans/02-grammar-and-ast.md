# Plan 02: Grammar と AST

## 目的

`sx` の parser と AST を、`sxi` と将来の `sxc` が共用できる形で定義する。
ここで grammar をぶらすと、interpreter と compiler が別言語になりやすい。

## 背景

初手の `sx` は source interpreter で動かす前提だが、
tree-walk evaluator だけを見て flat な構文に寄せると、
後で bytecode や native codegen へ広げにくい。
一方で compiler を意識しすぎると、v0 が過剰に複雑になる。

そこで parser は次の性質を持つ必要がある。

- declaration / statement / expression が明確に分かれる
- brace block と semicolon により recovery point を持てる
- REPL でも incomplete input を判定しやすい
- AST node が evaluation と codegen の両方に使える

## v0 grammar の中心

### declaration

- `fn name(params) -> type { ... }`
- `let name = expr;`
- top-level `let` と `fn`

### statement

- block
- expression statement
- `if (...) { ... } else { ... }`
- `while (...) { ... }`
- `return expr;`

### expression

- int / bool / string literal
- variable reference
- unary / binary operator
- function call
- indexing
- namespace access
- list literal
- map literal

`for`、`break`、`continue`、assignment expression、ternary operator は
v0 範囲に入れるか後段で判断する。

## AST 方針

AST は parser 直後に意味を持つ単位まで上げる。
少なくとも次を分ける。

- source file
- declaration
- statement
- expression
- type annotation
- import / module use
- diagnostic span

AST node は source span を必ず持ち、
diagnostic と runtime stack trace の両方で使えるようにする。

## parser 方針

### 1. recursive descent + Pratt parser

- declaration / statement は recursive descent
- expression は Pratt parser

この構成なら grammar 追加時の局所性が高く、
演算子優先順位も制御しやすい。

### 2. error recovery

recovery point は最初から意識する。

- `;`
- `}`
- file end

これにより 1 箇所の parse failure で
残り全部が読めなくなるのを避ける。

### 3. incomplete input

REPL を意識し、parser は次を区別できるようにする。

- success
- syntax error
- incomplete input

`{`、`(`、string 終端などが閉じていない場合に、
syntax error と即断しない。

## 非ゴール

- 初回から perfect error recovery を目指すこと
- user-defined operator や macro に備えた拡張点を過剰に入れること
- optimizer 向け SSA IR を grammar phase に押し込むこと

## 実装ステップ

1. grammar の statement / expression 範囲を固定する
2. token 種別と precedence table を定義する
3. AST node と source span を定義する
4. parser result に incomplete / error / success を持たせる
5. host fixture で grammar を固定する

## 変更対象

- 新規候補
  - `src/usr/include/sx_ast.h`
  - `src/usr/include/sx_parser.h`
  - `src/usr/lib/libsx/ast.c`
  - `src/usr/lib/libsx/parser.c`
  - `tests/test_sx_parser.c`
  - `tests/test_sx_ast.c`
- 関連
  - `specs/sx-language/TASKS.md`
  - `specs/sxi-runtime/plans/01-command-surface-and-frontend-integration.md`

## 検証

- host parser fixture
  - nested `if`
  - function declaration
  - list / map literal
  - namespace call
- recovery fixture
  - `;` 抜け
  - `}` 抜け
  - unclosed string
- REPL fixture
  - incomplete block
  - incomplete call

## 完了条件

- v0 grammar が 1 つの parser contract で説明できる
- AST node 種別と source span が安定する
- incomplete input を syntax error と区別できる
- `sxi` と `sxc` が共通 AST を前提に進められる
