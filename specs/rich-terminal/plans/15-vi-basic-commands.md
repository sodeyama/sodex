# Plan 15: vi の基本編集コマンド拡張

## 概要

Plan 12 で `vi` の起動、挿入、保存は成立し、
Plan 13 と 14 で UTF-8 表示と日本語直接入力も通るようになった。
ただし normal mode は `hjkl`、矢印、`i`、`:` を中心にした最小実装のままで、
既存ファイルを編集するには移動、削除、追記の基本操作がまだ足りない。

この plan では `vi` の normal mode を実用段階まで広げ、
既存テキストを開いて移動し、削除し、追記して保存する日常的な編集フローを成立させる。
初期ゴールは `x`、`dd`、`dw`、`0`、`$`、`w`、`b`、`e`、`gg`、`G`、
`a`、`A`、`I`、`o`、`O` を使って UTF-8 テキストを破綻なく編集できること。

## 依存と出口

- 依存: 09, 12, 13, 14
- この plan の出口
  - `vi` が複数キーの normal mode コマンドを解釈できる
  - `vi` が UTF-8 文字境界を壊さずに移動と削除を行える
  - `x`, `X`, `dd`, `D`, `dw`, `db`, `de`, `d0`, `d$` を実装できる
  - `0`, `^`, `$`, `w`, `b`, `e`, `gg`, `G` を実装できる
  - `a`, `A`, `I`, `o`, `O` により追記と行追加へすぐ入れる
  - host test と QEMU smoke で基本編集フローの回帰を検知できる

## 現状

- `vi` の normal mode は `hjkl`、矢印、`i`、`:` の最小 subset に留まっている
- `d` のような operator-pending と `gg` のような複合入力状態はまだ無い
- `vi_buffer` は挿入、改行、Backspace、上下左右移動まではあるが、
  単語境界移動や削除範囲計算の API が不足している
- UTF-8 / wide char の表示と insert mode 入力は成立しているが、
  normal mode の削除と単語移動はそれを前提に組み立て直す必要がある

## 方針

- `vim` 全互換ではなく、筋肉記憶で使われやすい基本操作を優先する
- まず buffer 側に UTF-8 文字境界を意識した移動・削除 primitive を追加する
- `vi.c` 側は「operator + motion」の最小状態機械を持ち、
  `d` と `g` 系の複合入力を解釈する
- 全画面再描画と status 表示の基本構造は維持し、編集コマンドに集中する
- count、register、undo/redo、検索、visual mode、text object は非ゴールとする

## 設計判断

- normal mode に pending 状態を追加する
  - `d` 待ち
  - `g` 待ち
- motion は `vi_buffer` の helper へ寄せる
  - 行頭
  - 行内の最初の非空白
  - 行末
  - 単語前進
  - 単語後退
  - 単語末尾
  - 先頭行
  - 末尾行
- 削除は可能な限り「範囲削除」としてまとめて扱う
  - `x` / `X` は 1 文字削除
  - `dw` / `db` / `de` / `d0` / `d$` は motion から範囲を得る
  - `dd` は行単位の special case として扱う
- `word` の初期定義は最小にする
  - 空白を区切りとして扱う
  - 記号と英数字の細かな `vi` 互換差異は後段階に回す
- `a`, `A`, `I`, `o`, `O` は insert mode への入口として先に揃える
  - 追記のために毎回 `l` や `i` を組み合わせなくて済む状態を優先する

## 実装ステップ

1. `vi_buffer` に行頭末尾移動、単語移動、範囲削除、行削除 helper を追加する
2. normal mode に pending 状態を追加し、`d` と `g` の複合入力を解釈できるようにする
3. `0`, `^`, `$`, `w`, `b`, `e`, `gg`, `G` を追加する
4. `x`, `X`, `dd`, `D`, `dw`, `db`, `de`, `d0`, `d$` を追加する
5. `a`, `A`, `I`, `o`, `O` を追加し、追記と行追加を成立させる
6. status 表示とモード遷移が破綻しないことを確認する
7. host test で ASCII / UTF-8 の移動と削除を固定する
8. QEMU smoke で「開く → 移動 → 削除 → 追記 → 保存」を固定する

## 変更対象

- 既存
  - `src/usr/command/vi.c`
  - `src/usr/include/vi.h`
  - `src/usr/lib/libc/vi_buffer.c`
  - `src/usr/lib/libc/vi_screen.c`
  - `tests/test_vi_buffer.c`
  - `src/test/run_qemu_vi_smoke.py`
- 必要なら更新
  - `specs/rich-terminal/TASKS.md`
  - `specs/rich-terminal/README.md`

## 検証

- `vi memo.txt` で既存ファイルを開き、`0`, `^`, `$`, `w`, `b`, `e`, `gg`, `G` で移動できる
- `x`, `X`, `dd`, `D`, `dw`, `db`, `de`, `d0`, `d$` で期待どおり削除できる
- `a`, `A`, `I`, `o`, `O` から insert mode に入り、UTF-8 テキストを追記できる
- multibyte 文字の途中でカーソルや削除範囲が止まらない
- `:wq` で保存した内容が期待どおりファイルへ反映される
- QEMU 上でも shell へ復帰し、その後のコマンド実行が継続できる

## 完了条件

- `vi` が「新規作成専用の最小 editor」から「既存ファイルも普通に直せる editor」へ進む
- normal mode の基本移動、削除、追記が UTF-8 前提で実用上成立する
- host test と QEMU smoke により基本編集コマンドの回帰を検知できる
