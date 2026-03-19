# Plan 21: IME の連文節 / 全文変換

## 概要

Plan 17/18 で guest 内 IME のかな入力、単一読みの漢字変換、大規模辞書は成立した。
ただし現在の IME は `ime_state.reading` 1 本と候補列 1 組だけを持ち、
`term` は入力済みかなを PTY へ流した後で backspace + 候補文字列へ置換している。
この構造では `かのじょはこきょうにもどりました` のような未確定入力全体を一度に変換したり、
文節ごとに候補を調整して最後に `Enter` で一括確定したりはできない。

この plan では `term` 常駐 IME を単一 clause 変換から一段進め、
未確定入力バッファ全体を対象にした連文節変換を追加する。
ここでいう「全文変換」は file 全体ではなく、
`term` がまだ PTY へ確定送信していない連続した未確定文字列全体を指す。
初期ゴールは shell と `vi` で、ひらがな文全体を変換し、
必要なら文節ごとに候補を調整して、最後は `Enter` で一括確定できること。

## 外部仕様の要点

- Apple の日本語入力は、複文節変換の方が変換回数が少なく精度も高まり、
  多くの場合は文全体を一度に入力して変換する方が正しい結果を得やすいとしている
- Apple の live / 従来変換はどちらも `Return` で候補を確定し、
  `Escape` でひらがなと変換下線へ戻れる
- Microsoft IME は `Space` で変換開始、候補ウィンドウ内の `Enter` で候補確定、
  `変換` キーで再変換を持つ
- Apple は再変換に app 側の対応が必要だと明記している
- 以上から、Google 日本語入力に近い体験を guest 側で再現するには
  「単一読み」ではなく「未確定入力全体 + 文節列 + focused clause」の state が必要である

## 依存と出口

- 依存: 18, 20
- この plan の出口
  - IME が未確定入力バッファ全体と文節列を保持できる
  - `Space` / `変換` で全文変換へ入り、文節 focus 移動と候補調整ができる
  - `Enter` が変換中の全文を一括確定し、未確定中の `Enter` は改行ではなく IME commit を優先する
  - shell と `vi` の両方で multi-clause UTF-8 commit が破綻しない
  - host test と QEMU smoke で全文変換、`Enter` 確定、キャンセル、文節移動の回帰を検知できる

## 実装後の状態

- `ime_state` は composition 全体、clause 配列、focused clause、phase を保持し、従来の単一 clause 変換もその特殊ケースとして扱える
- 辞書 blob は v3 で candidate cost を保持し、exact lookup と segmentation 用 lookup の両方で使える
- `term` は `Space` / `Left` / `Right` / `Shift+Left` / `Shift+Right` / `Enter` / `Esc` で全文変換、文節移動、境界調整、全文確定、ひらがな復帰を扱える
- shell / `vi` への commit は 1 回の UTF-8 burst で流し、composition 中の `Enter` は改行より IME commit を優先する
- host test では分節、候補切り替え、境界調整、全文 commit、cancel を固定済みである
- QEMU smoke script 側の追従は入っているが、通常 runlevel に入らず rescue 経由になる既知問題のため end-to-end の安定化は GitHub Issue #24 で追跡中である

## 着手前の現状

- `ime_state` は `reading`, `candidates`, `candidate_index`, `conversion_active` しか持たない
- `ime_conversion.c` は「完全一致の読み 1 件 -> 候補列 1 組」の helper であり、
  文節分割や複数 clause の focus を持たない
- `term.c` はかなを即座に PTY へ流し、変換確定時だけ backspace で置換する
- `Enter` は `conversion_active` 中だけ確定で、それ以外では IME flush 後に改行が流れる
- `mkimeblob.py` の blob v2 は読みと候補文字列だけを持ち、
  `build_ime_dictionary_source.py` が Mozc から読んでいる `cost` を runtime へ持ち越していない
- overlay は mode、1 本の読み、1 page の候補だけを表示する

## 方針

- IME の責務は引き続き `term` に置く
  - shell / `vi` は確定済み UTF-8 を受け取るだけに留める
- 変換単位は「最後に確定していない連続入力全体」に広げる
  - 単一 clause 変換はその特殊ケースとして残す
- 内部 state を 3 層に分ける
  - romaji preedit
  - 未確定かな composition 全体
  - composition を分節した clause 列と focused clause の候補状態
- 連文節の初期変換は自動分節で始める
  - `Space` / `変換` の初回で全文を clause 列へ切る
  - その後は focused clause の候補だけを前後に送る
- `Enter` を IME commit 優先へ寄せる
  - composition が残っている間は改行より確定を優先する
  - 改行は composition を空にした後の次の `Enter` で流す
- 既確定テキストの再変換は別レイヤとして切り分ける
  - app 非依存の再変換は非ゴール
  - `vi` 選択範囲など app 協調の hook は将来拡張点としてだけ定義する
- 既存の backspace 置換経路は段階移行を許す
  - まずは commit sink を抽象化し、multi-clause state machine を先に成立させる
  - 必要なら後で term 内未送信 composition へ寄せる

## 非ゴール

- 予測変換
- 学習辞書
- app 非依存の再変換
- 全ファイル / 全バッファを IME 単独で再変換すること
- クラウド問い合わせや外部 IME 依存
- 初期段階での自由カーソル移動つき composition 編集

## 設計判断

- `ime_state` 直下に clause 配列を増やすより、composition helper を分離する
  - `ime_composition`
  - `ime_segmenter`
  - `ime_clause_candidates`
  のような pure logic helper を分ける
- 自動分節は DP / Viterbi 風に「総コスト最小」の path を取る
  - 候補 cost は Mozc source 由来の順序 / 重みを blob v3 へ残す
  - 未一致部分は「ひらがなそのまま」の fallback clause で塞ぐ
- blob format は v3 へ拡張する
  - 読み完全一致だけでなく部分読み評価に必要な candidate cost を持つ
  - `build_ime_dictionary_source.py` が既に持っている Mozc `cost` を捨てない
- focused clause の基本操作は最小に絞る
  - `Space`: 次候補
  - `Shift+Space`: 前候補
  - `Left` / `Right`: focus clause 移動
  - `Shift+Left` / `Shift+Right`: clause 境界の縮小 / 拡張
  - `Enter`: composition 全体を確定
  - `Esc`: clause 変換を破棄してひらがな composition へ戻る
- overlay は本文と別に「全文 / focused clause / 候補 page」を見分けられる構成にする
  - 本文上は composition 全体を下線または色反転で示す
  - overlay は focused clause の読みと候補 page を出す
- shell completion より IME composition を優先する
  - IME active 中は completion を開始しない
- `vi` / shell への commit は 1 回の UTF-8 burst として扱う
  - 変換済み clause を clause ごとに個別送信しない
  - 途中 clause の候補調整で app 側 state を何度も書き換えない

## 実装フェーズ

### Phase A: composition state の再設計

1. `ime_state` と新規 helper header に composition 全体、clause 配列、focused clause、phase を定義する
2. 既存の `reading` / `candidates` 利用箇所を洗い出し、互換 field と新 field の境界を決める
3. romaji -> かなの未確定入力を、単一 `reading` ではなく composition buffer へ積む helper を切り出す
4. 既存の単一 clause 変換 test を壊さず、composition buffer が空のときは従来挙動へ戻せる足場を作る

### Phase B: 辞書 format と lookup の拡張

5. `build_ime_dictionary_source.py` に cost 付き中間表現を残し、blob v3 の入力形式を決める
6. `mkimeblob.py` と `ime_dict_blob.*` を更新し、candidate cost と substring lookup に必要な entry を読めるようにする
7. `ime_dictionary.*` に exact lookup と segmentation 用 lookup を追加し、blob 欠落時 fallback も維持する

### Phase C: 分節と clause 操作

8. composition 全体を clause 列へ切る `ime_segmenter` を pure logic として実装する
9. focused clause の候補遷移、focus 移動、境界調整、全文 commit / cancel を `ime_composition` helper にまとめる
10. 未一致かなをそのまま残す fallback clause と、長文時の clause 数 / 候補数 cap を定義する

### Phase D: `term` 統合

11. `term` の key 変換を更新し、全文変換開始、focused clause 操作、`Enter` commit 優先を実装する
12. inline 表示で composition 全体を本文上に示し、focused clause を視覚的に区別できるようにする
13. overlay を更新し、focused clause の読み、候補 page、全文中の位置を出せるようにする
14. shell completion との優先度を再確認し、IME active 中の completion 抑止を維持する

### Phase E: commit sink と検証

15. shell / `vi` 向けの commit sink を整理し、multi-clause commit を 1 回の UTF-8 burst で流す
16. composition 中の `Enter` は newline を送らず commit 優先、commit 後の次の `Enter` で改行に戻ることを固定する
17. host test で分節、候補切り替え、境界調整、`Enter` commit、`Esc` cancel を固定する
18. QEMU smoke で shell / `vi` の全文変換、保存、再表示を固定する

## 着手順の詳細

- 先に Phase A だけを入れても、まだ全文変換は有効化しない
  - state を増やす変更と入力経路の変更を分離し、回帰点を狭く保つ
- Phase B は blob v2 をすぐ捨てず、読み取り側で v2/v3 共存期間を許してよい
  - host test を先に揃えてから image 側の blob 差し替えへ進む
- Phase C の segmenter は最初から最適化しすぎない
  - まずは cost 最小 path が取れること
  - 次に fallback clause
  - 最後に境界調整
  の順で積む
- Phase D では `Enter` の意味変更が最も壊れやすい
  - shell prompt
  - `vi` insert mode
  - IME inactive
  の 3 条件を分けて確認する
- Phase E では host test で state machine を固めてから QEMU smoke を足す
  - QEMU では全文変換成功だけでなく、`Esc` cancel と clause 調整も 1 本は固定する

## 変更対象

- 既存
  - `src/usr/command/term.c`
  - `src/usr/include/ime.h`
  - `src/usr/include/ime_conversion.h`
  - `src/usr/include/ime_dictionary.h`
  - `src/usr/include/ime_dict_blob.h`
  - `src/usr/lib/libc/ime_romaji.c`
  - `src/usr/lib/libc/ime_conversion.c`
  - `src/usr/lib/libc/ime_dictionary.c`
  - `src/usr/lib/libc/ime_dict_blob.c`
  - `src/tools/build_ime_dictionary_source.py`
  - `src/tools/mkimeblob.py`
  - `src/test/run_qemu_ime_smoke.py`
  - `tests/test_ime_romaji.c`
  - `tests/test_ime_conversion.c`
- 新規候補
  - `src/usr/include/ime_composition.h`
  - `src/usr/lib/libc/ime_composition.c`
  - `src/usr/include/ime_segmenter.h`
  - `src/usr/lib/libc/ime_segmenter.c`
  - `tests/test_ime_composition.c`
  - `tests/test_ime_segmenter.c`
  - `tests/test_ime_dict_blob_v3.c`
  - `src/test/data/term_ime_sentence_reference.json`

## 検証

- `かのじょはこきょうにもどりました` を一度に入力し、
  `Space` / `変換` で `彼女は故郷に戻りました` へ全文変換できる
- multi-clause 変換中に `Left` / `Right` で focused clause を移動できる
- focused clause だけを `Space` / `Shift+Space` で候補変更でき、
  他 clause の選択は維持される
- `Shift+Left` / `Shift+Right` で clause 境界を調整して再分節できる
- composition が残っている間の `Enter` は newline ではなく IME commit を優先する
- `Esc` で clause 変換を破棄し、ひらがな composition に戻れる
- shell で日本語文を確定後に実行でき、`vi` insert mode でも保存して `cat` で再確認できる
- host / QEMU のどちらでも全文変換の回帰を継続検知できる

## 完了条件

- guest 内 IME が「単一読みの候補置換」から「未確定入力全体を分節して変換する IME」へ進む
- `Enter` 確定中心の変換フローが shell / `vi` で揃う
- 連文節変換の退行を host / QEMU で継続監視できる

## 参考

- [Apple: Macの日本語入力ソースを使用して変換精度を向上させる](https://support.apple.com/ja-jp/guide/japanese-input-method/jpim10257/mac)
- [Apple: Macで日本語入力ソースを使用して日本語を入力する](https://support.apple.com/ja-jp/guide/japanese-input-method/jpim10265/mac)
- [Apple: Macの日本語入力ソースの使用時に変換を元に戻す](https://support.apple.com/ja-jp/guide/japanese-input-method/jpim10309/mac)
- [Microsoft: Microsoft 日本語 IME](https://support.microsoft.com/ja-jp/windows/microsoft-%E6%97%A5%E6%9C%AC%E8%AA%9E-ime-da40471d-6b91-4042-ae8b-713a96476916)
