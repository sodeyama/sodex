# Plan 08: shell 統合と resize

## 概要

terminal client 導入後に、実際の boot path と shell をどうつなぐかを決める。
あわせて列数・行数の伝播と resize の扱いを定義する。

## 方針

- init は直接 `eshell` ではなく `term` を起動する
- `term` が PTY slave 上で `eshell` を起動する
- 初期段階では `COLUMNS`, `LINES` または簡易 syscall でサイズを渡す
- 将来 `SIGWINCH` 相当または `ioctl` を追加できる設計にする

## 実装ステップ

1. `src/usr/init.c` の起動対象を `term` 中心に変える
2. `eshell` の固定バッファや prompt 前提を見直す
3. 行編集の責務を shell 側から terminal / line discipline 側へ寄せる
4. resize 通知 API を設計する
5. 移行期間は旧コンソール shell を fallback として残す

## 変更対象

- 既存
  - `src/usr/init.c`
  - `src/usr/command/eshell.c`
  - `src/usr/include/eshell.h`
- 新規候補
  - `src/usr/include/winsize.h`

## 完了条件

- boot 後に terminal client 経由で shell が使える
- 120 列以上で prompt とコマンド表示が破綻しない
- resize 時に列数・行数が再計算できる

