# sodex

sodex は完全自作の実験用 OS です。もともとは 2006-2007 年頃に実機向けに作り始めたもので、現在は QEMU 上での開発と検証を中心に進めています。

最近の実装では、単純な VGA テキスト直書きの段階から進み、graphics terminal、TTY/PTY、VT parser、shell、`vi`、UTF-8 表示、日本語入力までを一通り扱える構成になっています。

## 現在できること

- QEMU 上で boot し、userland の `term` を既定 terminal として起動できる
- framebuffer 上で terminal を描画し、ANSI/VT の主要シーケンスを扱える
- TTY/PTY ベースで shell を動かせる
- `touch` `mkdir` `rm` `rmdir` `mv` による基本的な ext3 操作ができる
- shell で `|` `>` `<` を使える
- `vi` でファイルを編集し、`:w` `:q` `:wq` で保存できる
- `vi` の normal mode で `0`, `^`, `$`, `w`, `b`, `e`, `gg`, `G`, `x`, `X`, `dd`, `D`, `dw`, `db`, `de`, `d0`, `d$`, `a`, `A`, `I`, `o`, `O` を扱える
- UTF-8 と日本語を含むテキストを `term`、shell、`cat`、`vi` で表示・編集・保存できる
- guest 内 IME により、shell と `vi` でひらがな / カタカナを直接入力できる
- guest 内 IME の最小漢字変換と候補 UI を shell と `vi` で扱える
- host test と QEMU smoke test で主要導線を回帰検知できる

## まだやっていないこと

- GUI やウィンドウシステム
- `vi` の複数バッファ、text object、マクロ
- IME の大規模辞書、予測変換、学習辞書
- 一般ユーザー向けの install 手順や配布物の整備

## 必要なもの

- `python3`
- `qemu-system-i386`
- `x86_64-elf-gcc`, `x86_64-elf-as`, `x86_64-elf-ld`, `x86_64-elf-ar`
- `g++`

この repo の既定 `makefile.inc` は cross toolchain を `/opt/homebrew/bin` に置く前提です。必要ならローカル環境に合わせて [makefile.inc](makefile.inc) を調整してください。

## ビルド

```sh
make
```

生成物は主に `build/` 配下に出ます。QEMU 起動に使うディスクイメージは `build/bin/fsboot.bin` です。

## 起動

```sh
bin/start.sh
```

通常の `bin/start.sh` は `-display cocoa` を使うため、macOS 前提です。headless 実行は `bin/start.sh server-headless` を使ってください。
既定の QEMU RAM は `512MB` です。`SODEX_QEMU_MEM_MB=1024 bin/start.sh` のように上書きできます。

ネットワーク付きで起動したい場合:

```sh
bin/start.sh net
```

server runtime を headless で起動したい場合:

```sh
bin/start.sh server-headless
```

Docker 上で server runtime を常駐させたい場合:

```sh
docker build -f docker/server-runtime/Dockerfile -t sodex-server-runtime .
docker run --rm -p 18080:18080 -p 10023:10023 sodex-server-runtime
```

この image は `linux/amd64` 前提です。Apple Silicon などの arm64 host では Docker の emulation 経由で動かします。
published port の疎通は Linux host を前提にしています。Docker Desktop/macOS 上の nested slirp は未確認です。

`/dev/kvm` を使える Linux なら、`--device /dev/kvm -e SODEX_QEMU_ACCEL=kvm` を追加できます。

## テスト

host 側 unit test:

```sh
make test
```

kernel integration test:

```sh
make test-qemu
```

主な QEMU smoke test:

```sh
make -C src test-qemu-memory
make -C src test-qemu-user-memory
make -C src test-qemu-term
make -C src test-qemu-fs
make -C src test-qemu-shell-io
make -C src test-qemu-vi
make -C src test-qemu-ime
```

`make -C src test-qemu-memory` は `128/256/512/1024MB` の memory scaling matrix を回します。
`make -C src test-qemu-user-memory` は shell 経由で `memgrow` を起動し、`execve` と `malloc/brk` の userland 回帰を確認します。
guest が使う RAM を論理的に絞りたいときは、`SODEX_RAM_CAP_MB=256 make -C src test-qemu-memory` のように cap を付けられます。

## リポジトリの見どころ

- [src](src)
  カーネル、userland、terminal、`vi`、TTY/PTY、テスト用スクリプト本体
- [tests](tests)
  host 側 unit test
- [specs/rich-terminal/README.md](specs/rich-terminal/README.md)
  現在の terminal / shell / `vi` / UTF-8 / IME 拡張の全体計画
- [specs/rich-terminal/TASKS.md](specs/rich-terminal/TASKS.md)
  実装タスクの状態
- [specs/memory-scaling/README.md](specs/memory-scaling/README.md)
  QEMU の RAM 量と guest の allocator / paging をつなぐ拡張計画
- [specs/memory-scaling/TASKS.md](specs/memory-scaling/TASKS.md)
  高メモリ化の実装タスク

## 現状の読み方

ルート README は「今この repo が何をできるか」の要約です。詳細な設計判断や roadmap は [specs/rich-terminal/README.md](specs/rich-terminal/README.md) と [specs/memory-scaling/README.md](specs/memory-scaling/README.md) を参照してください。

## 過去の開発ログ

初期の `sodex` 開発時のメモや実装記録は、昔のブログ記事にも残っています。

- 記事一覧: https://sodex.hatenablog.com/archive/category/sodex

開始とリリース:

- 最初の記事: https://sodex.hatenablog.com/entry/20070406/1175809589
  開発の出発点と、当時の開発環境のメモです。
- `Sodex 0.0.1リリース`: https://sodex.hatenablog.com/entry/20071007/1191695763
  初期リリース時点での到達状況が分かります。

プロセス、ELF、シェル:

- `OS作成 - ページング＆プロセス編`: https://sodex.hatenablog.com/entry/20070713/1184312942
  `execve` とプロセス生成に手が入った時期の記事です。
- `OS作成 - elfローダ編`: https://sodex.hatenablog.com/entry/20070725/1185335342
  ELF ローダとユーザプロセス配置の話です。
- `OS作成 - シェル編`: https://sodex.hatenablog.com/entry/20070924/1190655818
  最初期の shell が動き始めた頃の記事です。

デバイス、ストレージ、boot:

- `OS作成 - NIC編`: https://sodex.hatenablog.com/entry/20080117/1200584746
  NE2000 NIC ドライバまわりの試行錯誤です。
- `usb mass storage`: https://sodex.hatenablog.com/entry/20090729/1248864705
  USB mass storage から 1 セクタ読めた時点の記録です。
- `USB boot`: https://sodex.hatenablog.com/entry/20090814/1250228357
  USB boot を詰めていた時期の記事です。

## フォントとライセンス

rich terminal の UTF-8 / 日本語表示では、`UDEV Gothic` を元に生成した bitmap font pack を使っています。

- 元フォントのライセンスは `SIL Open Font License 1.1`
- kernel には生の TTF/OTF ではなく、生成済み font pack を組み込む構成です
- ライセンス文書は [third_party/fonts/OFL-UDEV-Gothic.txt](third_party/fonts/OFL-UDEV-Gothic.txt) にあります
