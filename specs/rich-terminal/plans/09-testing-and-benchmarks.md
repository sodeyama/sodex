# Plan 09: テストと性能計測

## 概要

描画基盤、VT parser、PTY、terminal client は壊れ方が見えにくい。
そのため host 単体テスト、QEMU 結合テスト、描画性能計測を最初から持つ。

## 依存と出口

- 依存: 全体横断
- この plan の出口
  - pure logic の回帰が host test で回る
  - graphics terminal の boot smoke を QEMU で確認できる
  - 再描画コストを数値で比較できる

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

## 設計判断

- 既存の `tests/Makefile` 方式に合わせる
  - 新しい test runner を増やしすぎず、現在の流儀に乗せる
- pure logic は `tests/` 配下に寄せる
  - hardware I/O を mock して host で回す
- QEMU smoke は専用 runner を追加する
  - 既存 `src/test/run_qemu_ktest.py` を流用するか、terminal 専用 runner を別途作る
- 性能計測はまず wall-clock と描画回数のカウンタで十分
  - 高精度 profiler は不要

## 実装ステップ

1. terminal surface を純粋データ構造として切り出し、host テスト可能にする
2. key event 変換、TTY/PTY バッファ、VT parser の host test を追加する
3. VT fixture を用意し、入力列と期待 surface を比較する
4. QEMU 上で graphics mode から shell 起動までの smoke test を作る
5. framebuffer ダンプか比較用スナップショットの仕組みを用意する
6. 全面再描画、1 行スクロール、長文出力の計測カウンタを入れる
7. 最低限の性能目標と退行検知基準を文書化する

## 変更対象

- 新規候補
  - `tests/test_terminal_surface.c`
  - `tests/test_vt_parser.c`
  - `tests/test_tty.c`
  - `tests/fixtures/vt/`
  - `src/test/run_qemu_terminal_smoke.py`
  - `tests/test_keymap.c`
- 既存
  - `tests/Makefile`
  - `src/test/run_qemu_ktest.py`

## 検証

- `make -C tests test` で pure logic の回帰が回る
- QEMU smoke で graphics terminal 起動成功を検知できる
- 主要ケースの描画コストを比較できるレポートを残せる

## 完了条件

- parser と surface の回帰テストが host で回る
- QEMU で graphics terminal の smoke test がある
- 描画性能のボトルネックを数値で把握できる
