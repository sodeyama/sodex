# Plan 04: Diagnostics / Fixtures / Compatibility

## 目的

`sx` を「動くかどうか」だけでなく、
壊れたときに原因を切り分けやすい language にするために、
diagnostic format、fixture、sample corpus、互換ポリシーを先に固定する。

## 背景

agent が `sx` を書く場合、compile error や runtime error が
機械的に読み取りやすいことが重要になる。
また `sxi` と `sxc` を別実装で育てると、
同じ source に対する診断の差分が問題になりやすい。

最初から次をそろえておく必要がある。

- source span の持ち方
- human-readable diagnostic
- future JSON diagnostic の拡張余地
- valid / invalid fixture corpus
- versioning と compatibility の境界

## 方針

### 1. diagnostic は span first

最低限次を共通に持つ。

- path
- line
- column
- severity
- code
- message

human-readable では `path:line:column: error: ...` に寄せ、
JSON でも同じ source span を返せるようにする。

### 2. fixture は language spec の一部として扱う

fixture は実装おまけではなく、言語仕様の executable corpus とみなす。

- valid
- invalid
- warning candidate
- runtime sample

を分けて管理する。

### 3. examples は agent workflow を優先する

最低限次の examples を language corpus に入れる。

- hello
- file copy
- grep-lite
- JSON parse

この 4 本で file / loop / text / builtin surface を一通り踏める。

### 4. compatibility は language version を中心に管理する

`sxi` と `sxc` の version を直接そろえるのではなく、
`sx` language version を中心に置く。

- language version
- minimum `sxi` version
- minimum `sxc` version

の対応を表で持つ。

## 非ゴール

- 初回から formatter や linter を完備すること
- IDE protocol や language server を同時に作ること
- warning category を過剰に細分化すること

## 実装ステップ

1. source span と diagnostic struct を定義する
2. human-readable format を fix する
3. future JSON diagnostic の field を予約する
4. fixture corpus と sample programs を整える
5. versioning / compatibility policy を fix する

## 変更対象

- 新規候補
  - `src/usr/include/sx_diagnostic.h`
  - `src/usr/lib/libsx/diagnostic.c`
  - `tests/fixtures/sx/`
  - `tests/test_sx_diagnostic.c`
- 文書
  - `specs/sx-language/README.md`
  - `specs/sxi-runtime/README.md`
  - `specs/sxc-compiler/README.md`

## 検証

- invalid fixture で source span が安定する
- diagnostic message に source path と line/column が出る
- sample program が docs / fixture / runtime smoke で共有される
- version mismatch 時の挙動が文書化される

## 完了条件

- diagnostic と source span の契約が `sxi` / `sxc` 共通で説明できる
- fixture corpus が language の回帰基準として成立する
- 代表 sample が spec と実装の両方で使える
- language version 中心の互換ポリシーが定義される
