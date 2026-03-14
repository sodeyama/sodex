# Plan 02: 線形 framebuffer driver

## 概要

通常のターミナルらしい広い表示を得るため、VGA テキストモードではなく
QEMU/Bochs で扱いやすい線形 framebuffer を導入する。

## 方針

- 最初のターゲットは emulator 向けに割り切る
- 直接 VGA planar mode をいじるより、Bochs Graphics Adapter 相当の線形 framebuffer を優先する
- mode set 後は `width x height x bpp` の線形メモリに対して描画する

## 実装ステップ

1. QEMU/Bochs で使える graphics device 候補を確定する
   - 第一候補: Bochs Graphics Adapter 系
   - 代替: VBE 経由の線形 framebuffer
2. モード設定、VRAM base、pitch、bpp を取得できる driver を作る
3. `putpixel`, `fillrect`, `blit` を実装する
4. ダブルバッファか dirty rect 更新の方針を決める
5. fallback として VGA text backend を維持し、graphics 不可時は自動降格する

## 変更対象

- 新規候補
  - `src/drivers/bga.c`
  - `src/include/bga.h`
  - `src/display/fb.c`
  - `src/include/fb.h`
- 既存
  - `src/drivers/pci.c`
  - `src/kernel.c`

## 完了条件

- QEMU 上で graphics mode に入り、塗りつぶし描画ができる
- 解像度情報から列数・行数算出の前提が取れる
- graphics 不可時に既存表示へ戻れる

