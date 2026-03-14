# Plan 08: shell 統合と resize

## 概要

terminal client 導入後に、実際の boot path と shell をどうつなぐかを決める。
あわせて列数・行数の伝播と resize の扱いを定義する。

## 依存と出口

- 依存: 05, 06, 07
- この plan の出口
  - boot 後の既定経路が `term` になる
  - shell が現在の列数・行数を受け取れる
  - resize 後に terminal と shell の表示が再整合できる

## 方針

- init は直接 `eshell` ではなく `term` を起動する
- `term` が PTY slave 上で `eshell` を起動する
- 初期段階では `COLUMNS`, `LINES` または簡易 syscall でサイズを渡す
- 将来 `SIGWINCH` 相当または `ioctl` を追加できる設計にする

## 設計判断

- 移行期間は旧 `eshell` 直起動経路を残す
  - boot failure 時に復旧しやすくする
- winsize の初回伝播は簡易でよい
  - まずは起動時反映を優先し、後から変更通知を追加する
- resize 時は最初から scrollback 再折り返しをしない
  - まず visible surface の再計算と再描画に絞る
- `eshell` の行編集責務は縮小する
  - 長期的には TTY / terminal 側へ寄せる

## 実装ステップ

1. `src/usr/init.c` の起動対象を `term` 中心に変える
2. fallback 用に旧 `eshell` 直起動経路を残す
3. `term` から子 shell へ初期 winsize を渡す
4. `eshell` の固定長入力や prompt 前提を見直す
5. 行編集の責務を shell 側から TTY / terminal 側へ寄せる
6. resize 通知 API と再描画手順を定義する
7. resize 後に shell 側 winsize と terminal surface を再同期する

## 変更対象

- 既存
  - `src/usr/init.c`
  - `src/usr/command/eshell.c`
  - `src/usr/include/eshell.h`
- 新規候補
  - `src/usr/include/winsize.h`
  - `src/usr/lib/winsize.c`

## 検証

- boot 後に `term` 経由で shell が使える
- 120 列以上で prompt と長いコマンド入力が崩れにくい
- resize 後に列数・行数が再計算され、画面が再描画される

## 完了条件

- boot 後に terminal client 経由で shell が使える
- 120 列以上で prompt とコマンド表示が破綻しない
- resize 時に列数・行数が再計算できる
