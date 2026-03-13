# Testing & Refactoring Spec

ネットワーク機能追加前に、既存カーネルの品質を担保するためのテストケース作成とリファクタリング計画。

## 背景

Sodexは2006-2007年の実装で、テストコードがほぼ存在しない。
今後ネットワークスタック（network-driver spec）やHTTPクライアント、ブラウザと
大規模な機能追加を行うため、既存コードの安定性を先に確保する。

## 方針

1. **ホスト側ユニットテスト**: ハードウェア非依存の関数はホストOS（macOS）上でgccコンパイル＆実行
2. **QEMU統合テスト**: ハードウェア依存の機能はQEMU上でカーネル内テストコードを実行
3. **リファクタリング**: テストを書きながら重複コードの整理、マジックナンバーの定数化を進める

## コード分析サマリ

### ファイル規模

| 領域 | ファイル数 | 行数 | 備考 |
|------|-----------|------|------|
| カーネルコア（src/*.c） | 18 | ~4,000 | メモリ、プロセス、FS、syscall等 |
| ドライバ（src/drivers/） | 9 | ~2,100 | NE2000, UHCI, FDC, PCI等 |
| カーネルライブラリ（src/lib/） | 4 | ~280 | 文字列、メモリブロック |
| ネットワーク（src/net/） | 10 | ~5,500 | uIP TCP/IPスタック |
| ユーザー空間（src/usr/） | ~20 | ~1,400 | eshell, libc, コマンド |
| **合計** | **~60** | **~13,300** | |

### テスト可能性の分類

| カテゴリ | ファイル | テスト方式 |
|---------|---------|-----------|
| ハードウェア非依存 | lib/lib.c, lib/string.c, lib/memb.c | ホスト側ユニットテスト |
| グローバル状態依存 | memory.c, ext3fs.c, process.c | ホスト側（初期化関数付き） |
| ハードウェア依存 | vga.c, key.c, drivers/*, idt.c, page.c | QEMU統合テストのみ |
| ユーザー空間 | usr/lib/libc/*.c | ホスト側ユニットテスト |

### 既知の品質問題

- lib.c と string.c に**同一関数の重複実装**（strlen, strcmp等）
- マジックナンバー多数（memory.c, idt.c, vga.c）
- pic_eoi() の呼び出しで**引数欠落**（ne2000.c:80）
- エラーハンドリング不十分（syscall.c, ext3fs.c）
- グローバル変数の過度な使用

## Plans

| # | ファイル | 概要 | 優先度 |
|---|---------|------|--------|
| 01 | [01-test-framework.md](plans/01-test-framework.md) | ホスト側テストフレームワーク構築 | 高 |
| 02 | [02-lib-tests.md](plans/02-lib-tests.md) | src/lib/ のユニットテスト | 高 |
| 03 | [03-lib-refactor.md](plans/03-lib-refactor.md) | lib.c/string.c の重複解消 | 高 |
| 04 | [04-memory-tests.md](plans/04-memory-tests.md) | メモリ管理のユニットテスト | 高 |
| 05 | [05-memory-refactor.md](plans/05-memory-refactor.md) | メモリ管理のリファクタリング | 中 |
| 06 | [06-vga-refactor.md](plans/06-vga-refactor.md) | vga.c のテスタビリティ改善 | 中 |
| 07 | [07-ext3fs-tests.md](plans/07-ext3fs-tests.md) | ext3fsパース部分のユニットテスト | 中 |
| 08 | [08-qemu-integration.md](plans/08-qemu-integration.md) | QEMU統合テストフレームワーク | 低 |
| 09 | [09-constants-cleanup.md](plans/09-constants-cleanup.md) | マジックナンバーの定数化 | 低 |
