# Plan 17: 日本語 IME の漢字変換と候補 UI

## 概要

Plan 14 で guest 内 IME によるかな直接入力は成立した。
ただし現在の IME は `latin` / `hiragana` / `katakana` と preedit だけで、
読みから漢字へ変換したり、候補一覧から選んだりする機能は持っていない。

この plan では `term` 常駐 IME を一段進め、
かな読みの保持、辞書引き、候補 UI、確定 / キャンセル操作を追加する。
初期ゴールは shell と `vi` で、かな読みを漢字候補へ変換して確定保存できること。

## 依存と出口

- 依存: 09, 13, 14, 16
- この plan の出口
  - IME が未確定かな読みと確定済み UTF-8 を分けて保持できる
  - 読みから候補列を引き、複数候補を巡回できる
  - `term` が候補 UI を overlay として描画できる
  - shell の canonical 入力と `vi` の raw 入力の両方で、変換確定とキャンセルが破綻しない
  - host test と QEMU smoke で漢字変換と保存の回帰を検知できる

## 実装結果

- `ime_state` に読み、候補列、選択 index、変換状態を追加した
- 固定辞書で `にほんご -> 日本語`、`かんじ -> 漢字 / 感じ`、`へんかん -> 変換`、`あい -> 愛 / 藍` を引ける
- 変換開始、候補移動、確定、キャンセル、`Backspace` 復帰は `ime_conversion` helper へ分離した
- `term` は `Space` / `変換` で変換開始、`Space` / 左右で候補移動、`Enter` で確定、`Esc` / `Ctrl-G` でキャンセルできる
- overlay は framebuffer 上で `mode / index / 読み / 候補列` を UTF-8 で描き、選択候補を反転表示する
- `font16x16` の subset に候補表示用の漢字 glyph を追加し、候補 UI と確定後の表示を豆腐にしない
- host test と QEMU smoke で shell filename、`vi` 保存、候補 UI を固定した

## 現状

- `ime_state` は mode と preedit しか持たず、読み、候補、選択 index の概念がない
- `term` の overlay は mode label と preedit 表示までで、候補窓はない
- `Space` や `変換` は mode 切り替えや通常入力に使われ、変換操作としては未定義
- 辞書 format、候補順序、変換単位の設計が未着手である

## 方針

- IME の責務は引き続き `term` に置く
  - shell や `vi` は確定済み UTF-8 を受け取るだけに留める
- 初期段階の漢字変換は最小辞書でよい
  - 固定辞書
  - 完全一致の読み引き
  - 学習なし
- 候補 UI は terminal overlay として描く
  - 本文 PTY には未確定状態を流さない
- かな読みの確定、候補選択、キャンセルの操作を明確に分ける
  - `Space` / `変換` で変換開始
  - `Space` や矢印で候補移動
  - `Enter` で確定
  - `Esc` / `Ctrl-G` でキャンセル
- 予測変換、大規模辞書、学習辞書は非ゴールにする

## 設計判断

- IME 状態は 3 層に分ける
  - romaji preedit
  - かな読み
  - 候補選択中の変換状態
- 変換状態の遷移は `term` へ直接埋め込まず、pure logic helper へ分離する
  - 変換開始
  - 次候補 / 前候補
  - 確定
  - キャンセル
  - `Backspace` による読み編集への復帰
- 辞書はまず host build で生成するか、静的 table として組み込む
  - runtime での複雑な辞書編集は持ち込まない
- 初期辞書は「読み 1 件に対して候補列」を返す単純形式にする
  - TSV 風の静的データ、または等価な C table でよい
  - 文節分割や活用推定は持ち込まない
- 候補順序は初期段階では固定でよい
  - frequency 学習や context ranking は後段に回す
- 変換単位は最初は単一 clause に限定する
  - 文節分割や連文節変換は後回しにする
- 候補 UI は 1 行または数行の overlay に留める
  - 別 pane や別 screen は使わない
- overlay は少なくとも「mode / 読み / 候補列」を見分けられる構成にする
  - 選択中候補は反転表示などで本文と区別する
  - 候補が長い場合は行頭側を優先して切り詰める
- canonical / raw の両入力経路で commit / cancel の意味を揃える
- US 配列だけでも候補操作が完結するキー割り当てを残す
  - `Space` で次候補
  - `Shift+Space` または左 / 右で前後移動
  - `Enter` で確定
  - `Esc` / `Ctrl-G` でキャンセル

## 実装ステップ

1. `ime_state` を拡張し、かな読み、候補列、選択 index、変換状態を持てるようにする
2. 最小辞書 format と lookup 層を追加し、読みから候補を得られるようにする
3. 変換開始、候補遷移、確定、キャンセルを pure logic helper に切り出し、`term` 依存を薄くする
4. `term` の入力経路に変換開始、候補移動、確定、キャンセルの action を追加する
5. `term` overlay に候補 UI を追加し、preedit と候補列を見分けられるようにする
6. shell と `vi` で、候補選択後の確定 UTF-8 が保存まで通ることを確認する
7. host test で辞書 lookup、候補遷移、キャンセル、確定を固定する
8. QEMU smoke で変換、保存、再表示を固定する

## 変更対象

- 既存
  - `src/usr/include/ime.h`
  - `src/usr/lib/libc/ime_romaji.c`
  - `src/usr/command/term.c`
  - `src/usr/command/vi.c`
  - `src/tty/tty.c`
  - `tests/test_ime_romaji.c`
  - `src/test/run_qemu_ime_smoke.py`
- 新規候補
  - `src/usr/lib/libc/ime_conversion.c`
  - `src/usr/include/ime_conversion.h`
  - `src/usr/lib/libc/ime_dictionary.c`
  - `src/usr/include/ime_dictionary.h`
  - `tests/test_ime_conversion.c`
  - `tests/test_ime_dictionary.c`
  - `src/test/data/ime_candidates_reference.json`

## 検証

- `かな` 入力後に `Space` または `変換` で候補一覧へ入れる
- 候補一覧で次候補 / 前候補へ移動できる
- `Backspace` で候補選択を抜けて読み編集へ戻れる
- `Enter` で確定し、shell と `vi` が UTF-8 として受け取る
- `Esc` または `Ctrl-G` で候補選択を破棄し、かな読みへ戻れる
- 漢字を含む filename や本文を `vi` で保存し、`cat` で再確認できる
- QEMU 上でも候補 UI と保存結果を自動確認できる

## 完了条件

- guest 内 IME が「かな直接入力のみ」から「最小漢字変換つき IME」へ進む
- shell と `vi` で漢字を含む日本語入力を直接扱える
- 候補選択と確定の回帰を host / QEMU で継続的に検知できる
