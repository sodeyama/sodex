# Plan 07: ANSI/VT parser

## 概要

「通常のターミナルらしさ」を出すため、最低限の ANSI/VT100 シーケンスを userland の
terminal client で解釈する。

## 対象シーケンス

- `\r`, `\n`, `\b`, `\t`
- `ESC [ A/B/C/D` カーソル移動
- `ESC [ H`, `ESC [ f` カーソル位置
- `ESC [ J`, `ESC [ K` 画面/行消去
- `ESC [ m` 色属性
- `ESC [ s`, `ESC [ u` カーソル保存/復元

初期段階では扱わないもの:

- UTF-8 完全対応
- DEC private mode の広範な実装
- 複雑な端末問い合わせ

## 実装ステップ

1. byte stream から escape sequence を抽出する状態機械を作る
2. 通常文字と制御シーケンスを terminal surface 操作へ変換する
3. 色パレット 16 色をまず実装する
4. wrap、scroll、cursor move の競合を整理する
5. shell とアプリが壊れやすい `clear`, `cursor left/right`, `color reset` を先に通す

## 変更対象

- 新規候補
  - `src/usr/lib/vt_parser.c`
  - `src/usr/include/vt_parser.h`
  - `tests/test_vt_parser.c`
  - `tests/fixtures/vt/`

## 完了条件

- `clear`, `ls --color`, 簡単な full-screen redraw が扱える
- カーソル移動と色が破綻しない
- parser が userland 側で完結する

