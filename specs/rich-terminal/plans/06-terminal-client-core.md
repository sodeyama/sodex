# Plan 06: terminal client コア

## 概要

ユーザー空間に `/usr/bin/term` を導入し、描画、入力、PTY master の所有、
scrollback、再描画ループをここに集約する。

## 依存と出口

- 依存: 03, 04, 05
- この plan の出口
  - `/usr/bin/term` が build と image に入る
  - shell を子として起動し、出力を描画できる
  - terminal と shell の責務分離が目に見える形で成立する

## 方針

- GUI ではなく「フルスクリーン terminal client」
- terminal surface は userland に持つ
- kernel は framebuffer と input event と PTY を提供するだけに寄せる

## 設計判断

- 最初は single terminal / full-screen 専用にする
- event loop は `poll/select` 前提にしない
  - 使えない場合は nonblocking read と短い sleep/poll で成立させる
- scrollback は表示 surface と分離したリングバッファにする
- Plan 03 の surface / renderer ロジックは userland から使える形に寄せる
  - 必要なら複製ではなく共有モジュール化を検討する

## 実装ステップ

1. `/usr/bin/term` を build できるよう `src/usr/command/makefile` を更新する
2. `term` の起動引数、初期化、イベントループ骨格を作る
3. framebuffer を初期化し、`cols/rows` を計算する
4. PTY master を開き、子として `eshell` を起動する
5. PTY 出力を読み、surface と renderer に反映する
6. key event を受け取り、必要な VT 入力列へ変換して PTY に流す
7. damage tracking と scrollback リングを入れ、再描画量を抑える

## 変更対象

- 新規候補
  - `src/usr/command/term.c`
  - `src/usr/include/terminal.h`
  - `src/usr/lib/terminal_surface.c`
  - `src/usr/include/terminal_surface.h`
  - `src/usr/lib/vt_input.c`
- 既存
  - `src/usr/command/makefile`
  - `src/usr/makefile`
  - `src/usr/init.c`

## 検証

- `term` が build され、fs image に入る
- shell 出力が framebuffer 上に描画される
- 長文出力で scrollback が保持される

## 完了条件

- terminal client が shell を子として起動できる
- shell の出力が framebuffer 上に描画される
- スクロールバックを持ち、広い列数で表示できる
