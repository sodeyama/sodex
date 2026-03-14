# Plan 07: ANSI/VT parser

## 概要

「通常のターミナルらしさ」を出すため、最低限の ANSI/VT100 シーケンスを userland の
terminal client で解釈する。

## 依存と出口

- 依存: 06
- この plan の出口
  - `clear`, 色, カーソル移動, 行消去が使える
  - parser の振る舞いが fixture で固定される
  - shell だけでなく将来の text UI アプリに耐える最低限の terminal になる

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

## 設計判断

- parser は byte stream 状態機械と surface 操作実行部を分ける
- CSI パラメータの既定値を明示的に扱う
  - `CSI A` は `CSI 1 A` と同義、`m` は `0` を既定とする
- まず 16 色と基本属性に限定する
- scroll, wrap, cursor move の優先順位を test fixture で固定する

## 実装ステップ

1. byte stream から escape sequence を抽出する状態機械を作る
2. 通常文字と制御シーケンスを terminal surface 操作へ変換する
3. CSI パラメータ解析と既定値処理を実装する
4. `ESC [ m` の 16 色と reset を実装する
5. `clear`, `cursor left/right`, `erase line`, `home` を先に通す
6. wrap、scroll、cursor move の競合を fixture で固定する
7. `ls --color`, `printf`, `clear` の出力断片を fixture 化する

## 変更対象

- 新規候補
  - `src/usr/lib/vt_parser.c`
  - `src/usr/include/vt_parser.h`
  - `tests/test_vt_parser.c`
  - `tests/fixtures/vt/`

## 検証

- host test で fixture 比較が通る
- `clear` 実行後に画面内容とカーソル位置が期待通りになる
- 色リセットやカーソル移動で surface が破綻しない

## 完了条件

- `clear`, `ls --color`, 簡単な full-screen redraw が扱える
- カーソル移動と色が破綻しない
- parser が userland 側で完結する
