# Plan 09: テストと性能計測

## 概要

描画基盤、VT parser、PTY、terminal client は壊れ方が見えにくい。
そのため host 単体テスト、QEMU 結合テスト、描画性能計測を最初から持つ。

## テスト方針

1. host 単体
   - VT parser
   - terminal surface
   - key event 変換
   - tty/pty のリングバッファ
2. QEMU 結合
   - framebuffer 初期化
   - shell 起動
   - `clear`, 長行折り返し, scrollback
3. 見た目確認
   - framebuffer のダンプまたはスクリーンショット比較
4. 性能計測
   - 全面再描画
   - 1 行スクロール
   - 長文出力時の FPS 相当

## 実装ステップ

1. terminal surface を純粋データ構造として切り出し、host テスト可能にする
2. VT fixture を用意し、入力列と期待 surface を比較する
3. QEMU 上で graphics mode の smoke test を作る
4. framebuffer ダンプから差分比較できる仕組みを用意する
5. 描画コストを数値化し、全再描画を避ける最適化ポイントを測る

## 変更対象

- 新規候補
  - `tests/test_terminal_surface.c`
  - `tests/test_vt_parser.c`
  - `tests/test_tty.c`
  - `tests/fixtures/vt/`
  - `src/test/run_qemu_terminal_smoke.py`

## 完了条件

- parser と surface の回帰テストが host で回る
- QEMU で graphics terminal の smoke test がある
- 描画性能のボトルネックを数値で把握できる

