# 外部フォント

`sodex` の rich terminal 向けフォント生成では、`UDEV Gothic` を元フォントとして使う。
再生成を再現できるように、元の `ttf` と生成済み bitmap header を repo に含める。

## 元フォント

- フォント名: `UDEV Gothic`
- 利用バージョン: `v2.1.0`
- 配布元: <https://github.com/yuru7/udev-gothic>
- 利用ファイル: `UDEVGothic-Regular.ttf`

## 再生成手順

1. upstream release から `UDEVGothic_v2.1.0.zip` を取得する
2. `UDEVGothic-Regular.ttf` をこの directory に配置する
3. repo root で `make -C src regen-fonts` を実行する

## ライセンス

- 元フォントのライセンスは `SIL Open Font License 1.1`
- 生成済み bitmap header は `UDEV Gothic` 由来の派生生成物として扱う
- 配布時は [OFL-UDEV-Gothic.txt](/Users/sodeyama/git/sodex/third_party/fonts/OFL-UDEV-Gothic.txt) を同梱する
