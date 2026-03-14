# Plan 14: 日本語直接入力と IME

## 概要

Plan 13 で UTF-8 表示と保存は成立したが、キーボードからの日本語直接入力は未対応のまま残っている。
現在の `term` は raw key event を 1 byte ASCII または CSI へ変換して PTY に流すだけで、
host 側 IME の変換結果を guest へ渡す経路もない。

この plan では guest 内に最小 IME を持ち、
`term` 上で日本語の preedit、確定、モード切り替えを扱えるようにする。
初期ゴールは `vi` と shell で、ひらがな / カタカナを直接入力して保存できること。

## 依存と出口

- 依存: 04, 05, 08, 09, 13
- この plan の出口
  - `term` が ASCII 入力と日本語入力モードを切り替えられる
  - `term` が romaji からひらがな / カタカナを組み立てて UTF-8 として PTY へ送れる
  - shell の canonical 入力と `vi` の raw 入力の両方で、日本語テキストを壊さず扱える
  - IME の preedit と状態表示があり、利用者が現在の入力モードを判別できる
  - host test と QEMU smoke で切り替え、入力、保存を回帰検知できる

## 現状

- `struct key_event` は `ascii` が `u_int8_t` で、物理キー情報と文字入力が分離されていない
- `term` は printable key を 1 byte ASCII として PTY に書き込み、UTF-8 を生成しない
- TTY の canonical 編集は byte 単位で、Backspace も 1 byte 単位で消す
- QEMU は host 側 IME の確定文字列を guest へそのまま渡さないため、
  guest 内で IME を持たない限り `半角/全角` を押しても日本語入力にはならない

## 方針

- IME は kernel ではなく `term` に置く
  - kernel は raw key event の配送に留める
  - 文字変換、preedit、モード状態は userland terminal が持つ
- 入力を「物理キー」と「確定 UTF-8 テキスト」に分ける
  - 矢印、`Esc`、Ctrl 系はそのまま PTY へ流す
  - printable key だけを IME で解釈し、必要なら UTF-8 へ変換して送る
- 初期実装は辞書変換なしの最小 IME とする
  - `latin`
  - `hiragana`
  - `katakana`
- 切り替えキーは日本語キーボード専用に固定しない
  - まずは `Ctrl+Space` など US 配列でも押せる fallback を持つ
  - `半角/全角`、`変換`、`無変換` が取れるようになったら同じ操作へ束ねる
- preedit は `term` の overlay として描く
  - PTY 本文へ未確定文字列は流さない
  - shell / `vi` は確定済み UTF-8 だけを受け取る

## 設計判断

- `vi` や shell に IME 状態を持たせない
  - application は従来どおり byte stream を読むだけに留める
  - 日本語入力中かどうかは terminal 側だけが知る
- `term` 内に `ime_state` を追加し、raw key event を action へ変換してから PTY へ流す
  - `toggle`
  - `commit`
  - `backspace`
  - `pass-through`
- romaji 変換は pure logic として切り出す
  - `ka -> か`
  - `shi -> し`
  - `nn -> ん`
  - 小書き文字、促音、長音の最小対応を入れる
- TTY canonical の Backspace は UTF-8 文字境界で消す
  - multibyte を途中で削らず、最後の 1 文字単位で戻す
- 初期 plan では漢字変換と候補 UI は非ゴールにする
  - まず「かな入力で日本語を直接打てる」状態を完成させる
  - その後必要なら辞書変換 plan を分ける

## 実装ステップ

1. `term` の入力経路を整理し、raw key event から action を作る層を追加する
2. `ime_state` と mode 切り替えを実装し、`latin` / `hiragana` / `katakana` を持つ
3. romaji 変換器と preedit buffer を pure logic として追加する
4. `term` に IME overlay を描画し、未確定文字列と現在 mode を表示する
5. 確定文字列を UTF-8 として PTY へ流し、Ctrl / 矢印 / `Esc` は従来どおり通す
6. TTY canonical の行編集と echo を UTF-8 文字境界対応にする
7. `vi` insert mode と shell 入力で、日本語入力、Backspace、保存を確認する
8. host test と QEMU smoke で切り替えと入力フローを固定する

## 変更対象

- 既存
  - `src/include/key.h`
  - `src/key.c`
  - `src/usr/command/term.c`
  - `src/tty/tty.c`
  - `src/usr/lib/libc/utf8.c`
  - `src/usr/command/vi.c`
- 新規候補
  - `src/usr/lib/libc/ime_romaji.c`
  - `src/usr/include/ime.h`
  - `tests/test_ime_romaji.c`
  - `src/test/run_qemu_ime_smoke.py`

## 検証

- `Ctrl+Space` で IME mode を切り替えられる
- `nihongo` から `にほんご` を確定できる
- preedit 中の Backspace が romaji / かなの両方で破綻しない
- shell の 1 行入力で multibyte を含むファイル名や文字列を扱える
- `vi` で日本語を挿入して `:wq` で保存できる
- `Esc`、矢印、Ctrl 系ショートカットが IME 有効時でも壊れない
- QEMU 上で切り替え表示と UTF-8 保存結果を自動確認できる

## 完了条件

- `term` で日本語入力 mode を明示的に切り替えられる
- shell と `vi` に対して、日本語を UTF-8 として直接入力できる
- canonical / raw の両経路で multibyte 入力と Backspace が破綻しない
- host test と QEMU smoke が IME 回帰を検知できる
