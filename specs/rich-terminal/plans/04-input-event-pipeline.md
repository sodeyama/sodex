# Plan 04: 入力 event パイプライン

## 概要

現在の `key.c` は「文字列キュー」を返すだけで、矢印キー、修飾キー、キーアップ/ダウン、
繰り返し入力、ショートカットを扱えない。これを terminal client が使える key event 流へ変える。

## 現状

- `set_stdin()` が文字に変換した結果だけを保存する
- `get_stdin()` は行バッファ的に読む
- shell 側は canonical 入力前提で、terminal emulator 向け入力が存在しない

## 実装ステップ

1. `struct key_event { code, ascii, modifiers, pressed }` を定義する
2. scancode → key code → ascii 変換を段階化する
3. 文字列キューと event キューを分離する
4. 矢印、Home/End、PgUp/PgDn、Ctrl、Alt、Shift を扱う
5. terminal client が raw event を読める入口を作る

## 変更対象

- 既存
  - `src/key.c`
  - `src/include/key.h`
- 新規候補
  - `src/input/input_queue.c`
  - `src/include/input/event.h`
  - `tests/test_keymap.c`

## 完了条件

- key event と文字入力を分けて扱える
- terminal client が矢印や修飾キーを認識できる
- 旧 `stdin` 経路は移行期間だけ維持できる

