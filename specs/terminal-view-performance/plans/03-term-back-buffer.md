# Plan 03: local term の back buffer 化

## 概要

`term` の `render_surface()` が front buffer（framebuffer）へ直接描画する構造を改め、
off-screen pixel buffer（back buffer）に描画してから dirty rect を一括で front buffer へ copy する
double buffering 方式に変更する。

## 対象ファイル

- `src/usr/command/term.c` — render loop
- `src/usr/lib/libc/cell_renderer.c` — `cell_renderer_draw_cell()` の描画先
- `src/usr/include/cell_renderer.h` — renderer 構造体
- `tests/test_cell_renderer.c` — host 側の確認

## 現状の問題

- `cell_renderer_draw_cell()` が front buffer へ直接 `fill_rect` → `glyph` を描く
- 1 セルずつ処理するため、更新途中で「背景だけ塗られて文字がまだ乗っていない」状態が見える
- `term` の userland framebuffer 経路には present helper がない
- カーソル描画と IME overlay も front buffer に直接重ね描きしている

## 設計

### back buffer 確保（TVP-11）

`term` 起動時に framebuffer と同サイズの pixel buffer を `malloc` で確保する。
`cell_renderer` は front/back 両方の buffer を知り、
通常描画は back buffer へ向ける。

### dirty rect 収集と present（TVP-12）

`render_surface()` が dirty cell を描画する際に、描画したセルの bounding box を記録する。
全 dirty cell の描画完了後、dirty rect だけを
back buffer → front buffer へ `memcpy` する。

viewport resize 時には back buffer 再確保と再初期化まで面倒を見る。

最小実装は「全画面 copy」でもよい。dirty rect 最適化は後から入れられる。

### userland present（TVP-13）

kernel 側 `fb_flush()` の再定義には依存しない。
present は userland `term` / `cell_renderer` 側で完結させ、
`render_surface()` の末尾で 1 回だけ呼ぶ。

### カーソル・IME の統合（TVP-14）

カーソル描画と IME overlay も back buffer に描いてから flush する。
これにより、カーソル点滅や IME 候補の表示でも front buffer が直接変更されない。

## 実装ステップ

1. TVP-11: 描画先を back buffer に変更
2. TVP-12: back buffer の確保 / resize / dirty rect 整備
3. TVP-13: userland present 関数
4. TVP-14: カーソル・IME の back buffer 統合

## リスク

- back buffer のメモリ消費（1024x768x4 = 3MiB 程度）
  - Sodex のメモリ制約内で確保可能か確認が必要
- memcpy のコスト
  - dirty rect が小さければ全画面 copy より高速。全画面でも front buffer 直描きの flicker よりは良い
- viewport resize 後に front/back のサイズがずれるケース
  - 対策: `sync_viewport()` で再確保と full redraw をセットにする
