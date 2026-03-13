# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## プロジェクト概要

**Sodex** は、i486アーキテクチャ向けに完全にゼロから開発した独自OSカーネルです。2006-2007年に開発され、3.5インチFDD付きの実機でブート可能でした。現在はQEMU上での動作を想定しています。

**重要**: このカーネルは完全に独自実装であり、**外部ライブラリは一切使用できません**。すべての機能（メモリ管理、プロセス管理、デバイスドライバ、ファイルシステム、ネットワークスタックなど）を独自に実装しています。標準Cライブラリ（libc）も使用不可のため、`printf`、`malloc`、`memcpy`などの関数も自前で実装する必要があります。

### コアアーキテクチャ

**カーネル設計**:
- i486ベースの32ビットプロテクトモードカーネル
- ハイアーハーフ設計（カーネルは0xC0000000にマップ）
- カスタムブートローダチェーン: `boota.bin`（第1段階）→ `bootm.bin`（中間段階）→ `kernel.bin`
- 起動時からページングを有効化（初期8MB分の2x4MBページテーブル）
- カスタムリンカスクリプト（`boot.ld`）で物理/仮想アドレスマッピングを制御

**メモリ管理**:
- 仮想メモリは0xC0000000オフセット（`__PAGE_OFFSET`として定義）
- カーネルエントリ前に`startup.S`で初期ページディレクトリとテーブルを設定
- 独自メモリアロケータを`memory.c`に実装

**起動デバイスオプション**:
- USBブートモード: `bootusb.S`を第1段階ブートローダとして使用
- FDC（フロッピー）モード: `bootacient.S`を第1段階ブートローダとして使用
- ルート`makefile`の`DEVICE`変数で制御（デフォルト: USB_DEVICE）

**サブシステム**:
- **ドライバ**（`src/drivers/`）: PCI、UHCI USBホストコントローラ、SCSIエミュレーション、マスストレージ、フロッピー、NE2000ネットワーク、DMA、RS232C
- **ファイルシステム**（`src/ext3fs.c`、`src/fs.c`）: ext3ファイルシステムサポート
- **プロセス管理**（`src/process.c`）: プロセススケジューリング、実行（`execve.c`）、シグナル（`signal.c`）
- **システムコール**（`src/syscall.c`、`src/sys_core.S`）: システムコールインターフェース
- **ネットワーク**（`src/net/`）: uIP TCP/IPスタック実装
- **カーネルライブラリ**（`src/lib/`）: ユーティリティ関数、メモリプール、遅延処理

**セグメントセレクタ**（`sodex/const.h`より）:
- カーネルコード: 0x08、カーネルデータ: 0x10
- ユーザーコード: 0x23、ユーザーデータ: 0x2B

## ビルドシステム

### ツールチェーン要件
- クロスコンパイラ: `x86_64-elf-gcc`（`/opt/homebrew/bin/`に配置）
- クロスリンカ: `x86_64-elf-ld`
- ネイティブツール: `as`、`ar`、`g++`、`make`
- テスト用QEMU

### ビルドコマンド

**フルビルド**:
```bash
make
```
ビルド順序:
1. カーネルオブジェクトファイル（Cとアセンブリ）
2. ドライバサブシステム（`src/drivers/`）
3. カーネルライブラリ（`src/lib/`）
4. ネットワークスタック（`src/net/`）
5. ユーザー空間ライブラリとバイナリ（`src/usr/`）
6. ブートローダ各段階（第1、中間）
7. ビルドツール（`kmkfs`、`getsize`）
8. テストプロセス（`ptest`、`ptest2`）
9. `kmkfs`でファイルシステムイメージを作成

**クリーンビルド**:
```bash
make remake
# または
make clean && make
```

**個別コンポーネント**（`src/`ディレクトリ内で実行）:
```bash
make tools      # kmkfsとgetsizeユーティリティのみビルド
make ptest      # テストプロセスのみビルド
cd drivers && make   # ドライバのみビルド
cd lib && make       # カーネルライブラリのみビルド
cd net && make       # ネットワークスタックのみビルド
cd usr && make       # ユーザー空間コンポーネントのみビルド
```

**クリーン**:
```bash
make clean      # build/ディレクトリを削除（全ビルド成果物をクリーン）
```

### ビルド設定

起動デバイスを変更するには`makefile`を編集:
```makefile
DEVICE = USB_DEVICE   # USBブート（デフォルト）
# DEVICE = FDC_DEVICE # フロッピーブート
```

コンパイラフラグ（`makefile.inc`内）:
- `-m32`: 32ビットコンパイル
- `-nostdlib -ffreestanding -fno-builtin`: フリースタンディング環境
- `-fno-exceptions -fno-stack-protector`: カーネル向け設定
- `-g -O0`: デバッグシンボル、最適化なし

### ビルド出力

全ビルド成果物は`build/`ディレクトリに出力（out-of-treeビルド）:
- `build/bin/`: バイナリ出力
  - `kernel.bin`: メインカーネルバイナリ
  - `boota.bin`: 第1段階ブートローダ（カーネルサイズ情報を含む）
  - `bootm.bin`: 中間段階ブートローダ
  - `fsboot.bin`: ファイルシステムブートイメージ（kmkfsが作成）
- `build/obj/`: オブジェクトファイル（ソースツリー構造をミラー）
  - `build/obj/drivers/`, `build/obj/lib/`, `build/obj/net/`, `build/obj/usr/`
- `build/list/`: リンカマップ、リストファイル
- `build/tools/`: ビルドツール（`kmkfs`, `getsize`）
- `build/boot.ld`: プリプロセス済みリンカスクリプト

## 開発ワークフロー

### ファイル構成
- コアカーネル: `src/*.c`、`src/*.S`
- ヘッダ: `src/include/*.h`、`src/include/sodex/*.h`
- アセンブリヘルパー: `startup.S`（起動設定）、`sys_core.S`（システムコール）、`ihandlers.S`（割り込み）、`page_asm.S`（ページング）
- リンカ制御: `boot.ld.S`（プリプロセスされて`boot.ld`になる）

### 主要エントリポイント
1. **ブート**: `bootusb.S`または`bootacient.S` → ブートローダをロード
2. **中間段階**: `bootmiddle.S` → プロテクトモード設定
3. **カーネル初期化**: `startup.S` → ページング、GDT設定 → `start_kernel()`にジャンプ
4. **カーネルメイン**: `kernel.c:start_kernel()` → 全サブシステムを初期化

### 新機能の追加

**新しいCソースファイル**:
- `src/`ディレクトリに追加
- `src/include/`から適切なヘッダをインクルード
- `OBJS = $(subst .c,.o,$(wildcard *.c))`で自動的に拾われる
- `static`や`extern`の代わりに`PUBLIC`/`PRIVATE`マクロを使用（`sodex/const.h`で定義）

**新しいアセンブリファイル**:
- 特別な扱いが必要な場合は`makefile`の`ASMSRC`変数に追加
- そうでなければ`src/`に`.S`拡張子で配置

**新しいドライバ**:
- `src/drivers/`に追加
- `src/drivers/makefile`を更新
- `kernel.c:start_kernel()`で初期化

**新しいヘッダ**:
- 公開インターフェース: `src/include/*.h`
- カーネル内部定義: `src/include/sodex/*.h`

### QEMUでのテスト

ビルドと実行（ソースに指定なし - 一般的な使用法）:
```bash
make
qemu-system-i386 -fda build/bin/fsboot.bin  # FDDモード用
# またはUSBモードの場合、ブータブルUSBイメージを作成して-hdaを使用
```

## 重要なアーキテクチャ詳細

### 仮想メモリレイアウト
- カーネル仮想ベース: 0xC0000000（3GB）
- 物理メモリはカーネル仮想ベースから始まる恒等マッピング
- すべてのカーネルコード/データは仮想アドレス ≥ 0xC0000000を使用
- リンカスクリプトがAT()ディレクティブで物理配置を処理

### ブートローダチェーン
1. **第1段階**（`boota.bin`）: 中間段階をロード、`KERNEL_SECTS`でサイズ埋め込み
2. **中間段階**（`bootm.bin`）: プロテクトモード設定、カーネルをロード
3. **カーネル**（`kernel.bin`）: メインOSコード

`kmkfs`ツールがすべての段階を`fsboot.bin`ファイルシステムイメージに結合します。

### ビルドツール
- `kmkfs.cpp`: ブートローダとカーネルを含むファイルシステムイメージを作成
- `getsize.c`: ブートローダ用にカーネルサイズをセクタ数で計算

### 初期化シーケンス（kernel.cより）
1. 画面（VGA）
2. GDT（グローバルディスクリプタテーブル）
3. IDT（割り込みディスクリプタテーブル）とハンドラ
4. PIT（プログラマブルインターバルタイマー）
5. メモリ管理
6. キーボード
7. DMA
8. FDC（FDC_DEVICEの場合）またはUHCI/SCSI（USB_DEVICEの場合）
9. ページング
10. PCI
11. ext3ファイルシステム
12. システムコール
13. プロセス管理
14. シグナルハンドリング

### コード規約
- `static`、`extern`ではなく`PUBLIC`、`PRIVATE`、`EXTERN`マクロを使用
- 4KBブロックサイズ（`BLOCK_SIZE = 4096`）
- カーネルセグメントセレクタ: CS=0x08、DS=0x10
- ユーザーセグメントセレクタ: CS=0x23、DS=0x2B

### 外部ライブラリの制限
- **標準Cライブラリ不可**: `printf`、`malloc`、`free`、`memcpy`、`strlen`などは自前実装が必要
- **コンパイラフラグ**: `-nostdlib -ffreestanding -fno-builtin`で外部依存を排除
- **完全独自実装**: すべての機能をゼロから実装（メモリ管理、文字列操作、数学関数など）
- 新機能追加時は、既存の`src/lib/`内の実装パターンに従うこと
