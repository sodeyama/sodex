# Plan 02: 線形 framebuffer driver

## 概要

通常のターミナルらしい広い表示を得るため、VGA テキストモードではなく
QEMU/Bochs で扱いやすい線形 framebuffer を導入する。

## 依存と出口

- 依存: 01
- この plan の出口
  - graphics mode への切り替えができる
  - `fb_info` と基本描画 primitive が使える
  - graphics 初期化失敗時は VGA text backend に戻せる

## 方針

- 最初のターゲットは emulator 向けに割り切る
- 直接 VGA planar mode をいじるより、Bochs Graphics Adapter 相当の線形 framebuffer を優先する
- mode set 後は `width x height x bpp` の線形メモリに対して描画する

## 設計判断

- framebuffer 層は terminal 機能を持たない
  - `putpixel`, `fillrect`, `blit`, `flush` に留める
- 最初の解像度は固定候補から選ぶ
  - 例: `1024x768x32` を第一候補にし、失敗時は段階的に下げる
- kernel 初期化時に graphics 可否を決める
  - 後続の terminal client は「使える framebuffer があるか」だけを見ればよい
- fallback は必須にする
  - QEMU 設定差異や mode set 失敗で boot を止めない

## 実装ステップ

1. graphics device の検出方法を確定する
   - 第一候補は Bochs Graphics Adapter 系
   - 代替経路は VBE 互換を検討する
2. device 初期化と mode set を行う driver を作る
3. `width`, `height`, `pitch`, `bpp`, `base` を持つ `fb_info` を定義する
4. `putpixel`, `fillrect`, `blit`, `clear`, `flush` を実装する
5. 1 回の bring-up 用にテストパターン描画経路を作る
6. kernel 起動時に graphics backend を優先し、失敗時は VGA text backend へ自動降格する
7. QEMU smoke test から graphics 初期化成功を観測できるログか終了条件を用意する

## 変更対象

- 新規候補
  - `src/drivers/bga.c`
  - `src/include/bga.h`
  - `src/display/fb.c`
  - `src/include/fb.h`
  - `src/display/fb_backend.c`
- 既存
  - `src/drivers/pci.c`
  - `src/kernel.c`
  - `src/vga.c`

## 検証

- QEMU 上で graphics mode に入り、矩形塗りつぶしができる
- mode set 失敗時に従来 console へ戻る
- framebuffer 情報から後続 plan が列数・行数を算出できる

## 完了条件

- QEMU 上で graphics mode に入り、塗りつぶし描画ができる
- 解像度情報から列数・行数算出の前提が取れる
- graphics 不可時に既存表示へ戻れる
