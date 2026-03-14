# Plan 04: 入力 event パイプライン

## 概要

現在の `key.c` は「文字列キュー」を返すだけで、矢印キー、修飾キー、キーアップ/ダウン、
繰り返し入力、ショートカットを扱えない。これを terminal client が使える key event 流へ変える。

## 依存と出口

- 依存: 01
- この plan の出口
  - raw key event を読める
  - 従来の `stdin` 経路は互換 adapter として残る
  - 後続 plan が矢印キーや Ctrl を terminal 入力として扱える

## 現状

- `set_stdin()` が文字に変換した結果だけを保存する
- `get_stdin()` は行バッファ的に読む
- shell 側は canonical 入力前提で、terminal emulator 向け入力が存在しない

## 設計判断

- 入力は `scan code -> key code -> key event -> text` の段階で分ける
  - 文字列化は最終段だけに限定する
- key event には `pressed` と `modifiers` を持たせる
  - 後続で shortcut、raw mode、VT 入力列生成に使う
- 互換のため text queue はすぐには消さない
  - line discipline 導入までは adapter として維持する
- 最初の実装では US キーボード前提でよい
  - keymap 多言語化は後回し

## 実装ステップ

1. `struct key_event { code, ascii, modifiers, pressed }` を定義する
2. keyboard IRQ 経路で scan code から key code への変換を段階化する
3. modifier state を保持し、Shift / Ctrl / Alt の状態を event に反映する
4. text queue と event queue を分離する
5. 矢印、Home/End、PgUp/PgDn、Ctrl、Alt、Shift を最小対応する
6. terminal client が raw event を読める syscall または special fd を設計する
7. `set_stdin()` / `get_stdin()` は event からの互換 adapter に落とす

## 変更対象

- 既存
  - `src/key.c`
  - `src/include/key.h`
  - `src/idt.c`
- 新規候補
  - `src/input/input_queue.c`
  - `src/include/input/event.h`
  - `tests/test_keymap.c`

## 検証

- 矢印キーと修飾キーを raw event として取得できる
- 既存 `eshell` は互換経路で引き続き動く
- keymap の pure logic を host test で確認できる

## 完了条件

- key event と文字入力を分けて扱える
- terminal client が矢印や修飾キーを認識できる
- 旧 `stdin` 経路は移行期間だけ維持できる
