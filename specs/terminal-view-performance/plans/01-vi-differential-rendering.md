# Plan 01: vi 差分描画化

## 概要

`vi_screen_redraw()` の `ESC[2J` 全消去 + 全行再出力を廃止し、
前回描画との差分だけを VT sequence で出力する方式に変更する。
local / SSH 双方で最大の flicker 要因を解消する。

## 対象ファイル

- `src/usr/lib/libc/vi_screen.c` — redraw ロジック本体
- `src/usr/command/vi.c` — event loop
- `src/usr/include/vi.h` — public API と必要最小限の宣言

## 設計

### private な可視フレーム state

`vi_screen` 用の public 構造体は新設しない。
代わりに `vi_screen.c` 内部で、visible rows と status / command 行を保持する
private state を持つ。

保持対象は raw source line ではなく、次を反映した「表示済みフレーム」にする。

- `row_offset`
- UTF-8 の表示幅
- visual / visual-line の反転表示
- status 行と command/search 行
- 画面サイズ変更時の切り詰め結果

### dirty row 検出

行単位で「前回表示フレームの row」と「今回表示フレームの row」を比較し、
一致する行は skip する。
初回描画、viewport 変更、alternate screen 入退場直後は full redraw fallback を許す。

### dirty span 検出

dirty row 内で、表示列ベースで差分 span を検出する。
単純な `strcmp` ではなく、wide char と visual 反転を考慮した表示結果ベースで
開始列〜終了列を求める。

`ESC[row;colH` でカーソルを移動し、差分文字列だけ出力する。
行末の余白が減った場合は `ESC[K` を使う。

### status / command / cursor の独立管理

status 行と command/search 行は本文とは別 row として管理する。
本文に変更がなく mode や command だけ変わった場合でも、
本文全体を redraw しない。

cursor move は本文 row update と独立に最後に 1 回だけ出す。

### vi 起動/終了の画面管理

- 起動時: `ESC[?1049h`（alternate screen 入場）+ 初回 full draw
- 終了時: `ESC[?1049l`（alternate screen 退場）で shell 画面を復帰
- 不整合回復時: その frame だけ full redraw fallback を許す

## 実装ステップ

1. TVP-01: private な可視フレーム state 追加
2. TVP-02: 可視フレーム組み立て
3. TVP-03: `ESC[2J` 除去 + dirty row 検出
4. TVP-04: dirty span 検出 + 差分出力
5. TVP-05: status / command / cursor / restore / fallback 整理

## リスク

- vi の既存コマンド（`dd`, `o`, `p`, visual 選択、検索）で表示モデルとの同期がずれるケース
  - 対策: 不確実な操作は row 単位または frame 単位 fallback を許す
- raw line 差分だけでは visual 反転や wide char を正しく扱えない
  - 対策: source line ではなく表示済みフレームを比較対象にする
- public API を増やしすぎると `vi` 本体との責務境界が崩れる
  - 対策: state は `vi_screen.c` 内部に閉じる
