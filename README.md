# sodex

2006,7年くらいに作った完全自作OSです。
今は無き3.5インチFDD付きの実機でブートでできました
仮想環境はQemuで動きます

## 外部フォント利用予定

rich terminal の UTF-8 / 日本語表示では、`UDEV Gothic` を元に生成した
bitmap font pack を使う計画です。

- 元フォントのライセンスは `SIL Open Font License 1.1`
- kernel には生の TTF/OTF ではなく、生成済み font pack を組み込む想定
- 配布時は OFL 文書と attribution を同梱する前提
- 生成済み font pack は `UDEV Gothic` そのものの名前ではなく、別名で扱う前提
