# sodex

sodex は完全自作の実験用 OS です。もともとは 2006-2007 年頃に作ってました。
今はなきフロッピーデバイスとUSBで実機ブート出来てました。

## 元々できていたこと（2006-2007 年の初期実装）

- i486 protected mode カーネル（GDT、IDT、ページング、PIT）
- 独自ブートローダ（FDD / USB の 2 段階ブート → カーネルロード）
- 物理 / 仮想メモリ管理（カーネル 0xC0000000 ハイアーハーフ配置）
- プロセス管理（TSS タスクスイッチ、ELF ローダ、`execve`、`fork`、シグナル）
- ext3 ファイルシステム（inode、ディレクトリ操作、読み書き）
- VGA テキストモード画面出力
- キーボード入力、シリアル（RS-232C）出力
- 基本シェル（`eshell`）と userland コマンド（`ls`、`cat`、`ps`、`kill`、`pwd`）
- デバイスドライバ（PCI、DMA、FDC、UHCI USB、SCSI、マスストレージ、NE2000 NIC）
- uIP TCP/IP スタック（ARP、ルーティング）
- userland libc（`malloc`、`printf`、`string`、`regex`、`sbrk`）
- システムコールインターフェース
- ビルドツール（`kmkfs` ディスクイメージ作成、`getsize`）

## 現在できること

**カーネル & ブート**
- i486 32-bit protected mode カーネルが QEMU 上で boot する
- 独自ブートローダチェーン（第1段階 → 中間段階 → カーネル）
- ページング、GDT/IDT、PIT、メモリ管理、プロセス管理を独自実装

**グラフィカルターミナル**
- framebuffer 上に描画する rich terminal（VT100/ANSI エスケープシーケンス対応）
- TTY/PTY ベースの shell（パイプ `|`、リダイレクト `>` `<` `>>`）
- UTF-8 表示、日本語 IME（ひらがな / カタカナ / 漢字変換）

**ファイルシステム & コマンド**
- ext3 ファイルシステム（`ls` `cat` `touch` `mkdir` `rm` `rmdir` `mv` `cd` `pwd`）
- `vi` エディタ（normal/insert/command モード、undo/redo、ファイル保存）

**ネットワーク & サーバランタイム**
- uIP TCP/IP スタック（NE2000 NIC ドライバ）
- SSH サーバ（curve25519 鍵交換、Ed25519 host key、パスワード認証、PTY セッション）
- host (macOS) から README 記載の `ssh` 手順で guest shell に接続可能
- HTTP サーバ（port 8080）、Admin サーバ（port 10023）、Debug Shell（port 10024）

**デバイスドライバ**
- PCI、ATA、UHCI USB ホストコントローラ、USB マスストレージ、SCSI
- NE2000 NIC、DMA、RS-232C シリアル、BOCHS VGA

**サーバランタイム & デプロイ**
- headless モード（`bin/start.sh server-headless`）で GUI なし運用
- Docker コンテナ上での server runtime 常駐（ARM64/x86_64 対応）
- ポートフォワーディングで host から全サーバにアクセス可能

**暗号 & セキュリティ**
- AES、SHA-256、Ed25519（TweetNaCl）
- トークン認証、IP allowlist

**テスト**
- host 側 unit test + QEMU smoke test（boot、メモリ、FS、shell、vi、IME、SSH）

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
mkdir -p build/log/server-runtime
docker run --rm \
  -p 18080:18080 \
  -p 10023:10023 \
  -v "$(pwd)/build/log/server-runtime:/var/log/sodex" \
  sodex-server-runtime
```

この image は container 内で `i686-linux-gnu` cross toolchain を使って guest を build します。
Apple Silicon を含む arm64 host でも native image として build / run できます。

published port の peer IP は host により異なります。
Linux host の既定は `10.0.2.2`、Docker Desktop/macOS では `192.168.65.1` を確認しました。
allowlist を変えるときは `SODEX_ADMIN_ALLOW_IP=...` を指定してください。

ready 条件は container の serial/stdout に `AUDIT server_runtime_ready ...` が出た時点です。
`qemu_debug.log` と `monitor.sock` は mount した `/var/log/sodex` 配下に出ます。

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

Docker/headless server runtime smoke:

```sh
make test-docker-server
```

ポート衝突を避けたいときは `SODEX_HOST_HTTP_PORT` / `SODEX_HOST_ADMIN_PORT` を上書きできます。
Docker Desktop/macOS では `SODEX_ADMIN_ALLOW_IP=192.168.65.1 make test-docker-server` を使います。

QEMU debug shell smoke:

```sh
make test-qemu-debug-shell
```

debug shell の guest port は `10024` です。
host 側ポートを変えたいときは `SODEX_HOST_DEBUG_SHELL_PORT=11024 make test-qemu-debug-shell` のように上書きできます。

raw TCP client から手で試すときの最小例:

```sh
printf 'TOKEN control-secret\n' | nc 127.0.0.1 10024
```

返答は最初に `OK shell` が 1 行返り、その後は line protocol ではなく raw stream に切り替わります。
対話用途では `nc` より `socat` の方が扱いやすいです。

```sh
socat -,rawer,echo=0 TCP:127.0.0.1:10024
```

接続したら最初の 1 行で `TOKEN control-secret` を送り、以後は shell 入力をそのまま流します。

QEMU SSH smoke:

```sh
make test-qemu-ssh
```

host 側ポートを変えたいときは `SODEX_HOST_SSH_PORT=11022 make test-qemu-ssh` のように上書きできます。

手で試すときは、まず overlay を含めて guest を SSH 付きで作り直して起動します。

```sh
bin/restart.sh server-headless --ssh
```

その後、別ターミナルから接続します。
現時点では最小実装なので、auth method と host key 確認を明示した `ssh -tt` を前提にします。

```sh
ssh -tt -F /dev/null \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -p 10022 root@127.0.0.1
```

既定 password は `root-secret` です。`SODEX_SSH_PASSWORD` を export している場合はその値で上書きされます。
login 後は `sodex />` prompt が出るので、`ls` / `pwd` / `cat` を実行でき、
`Backspace`, `Ctrl-C`, `exit` も通ります。

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
