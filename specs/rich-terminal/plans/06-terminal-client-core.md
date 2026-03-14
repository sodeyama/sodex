# Plan 06: terminal client コア

## 概要

ユーザー空間に `/usr/bin/term` を導入し、描画、入力、PTY master の所有、
scrollback、再描画ループをここに集約する。

## 方針

- GUI ではなく「フルスクリーン terminal client」
- terminal surface は userland に持つ
- kernel は framebuffer と input event と PTY を提供するだけに寄せる

## 実装ステップ

1. `/usr/bin/term` の起動とイベントループを作る
2. framebuffer backend を初期化し、現在の `cols/rows` を計算する
3. PTY master からの出力を読み、surface に反映する
4. key event を受け取り、必要な入力列へ変換して PTY に流す
5. damage tracking で必要な範囲だけ再描画する
6. スクロールバック用の行リングバッファを導入する

## 変更対象

- 新規候補
  - `src/usr/command/term.c`
  - `src/usr/include/terminal.h`
  - `src/usr/lib/terminal_surface.c`
  - `src/usr/include/terminal_surface.h`

## 完了条件

- terminal client が shell を子として起動できる
- shell の出力が framebuffer 上に描画される
- スクロールバックを持ち、広い列数で表示できる

