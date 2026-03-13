# Plan 09: マジックナンバーの定数化

## 概要

コード全体に散在するマジックナンバーを名前付き定数に置き換え、可読性と保守性を改善する。

## 対象箇所

### memory.c

```c
// 変更前
#define KERNEL_MEMEND  0xc2000000
#define KERNEL_PMEMBASE 0x2000000
#define MIN_MEMSIZE 32

// 変更後
#define KERNEL_HEAP_VADDR   0xC2000000  // カーネルヒープ開始仮想アドレス
#define KERNEL_HEAP_PADDR   0x02000000  // カーネルヒープ開始物理アドレス
#define KALLOC_MIN_SIZE     32          // 最小割り当て単位（バイト）
```

### idt.c

```c
// 変更前
#define TYPE_INTR_GATE 0xEE
#define TYPE_TRAP_GATE 0xEF
#define TYPE_TASK_GATE 0x85

// 変更後（既にマクロだが、コメントで意味を明記）
#define TYPE_INTR_GATE 0xEE  // DPL=3, 32-bit interrupt gate
#define TYPE_TRAP_GATE 0xEF  // DPL=3, 32-bit trap gate
#define TYPE_TASK_GATE 0x85  // DPL=0, task gate
```

PIC関連:
```c
// 変更前
out8(0x20, 0x60 + irq);
out8(0xA0, 0x60 + irq - 8);
out8(0x20, 0x62);

// 変更後
#define PIC1_CMD  0x20
#define PIC2_CMD  0xA0
#define PIC_EOI_BASE 0x60

out8(PIC1_CMD, PIC_EOI_BASE + irq);
out8(PIC2_CMD, PIC_EOI_BASE + irq - 8);
out8(PIC1_CMD, PIC_EOI_BASE + 2);  // Cascade IRQ2 EOI
```

### vga.c

```c
// 変更前
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
char gColor = 0x2;

// 変更後
#define VGA_COLS     80
#define VGA_ROWS     25
#define VGA_COLOR_DEFAULT  0x02  // 緑on黒
#define VGA_COLOR_WHITE    0x07  // 白on黒
#define VGA_COLOR_ERROR    0x04  // 赤on黒
```

### ne2000.c

```c
// 変更前
out8(io_base+O_CR, 0x26);  // 意味不明

// 変更後
// 0x26 = CR_RD2 | CR_STA | ???
// ドキュメントと照合して正しい定数の組み合わせにする
out8(io_base + O_CR, CR_PAGE0 | CR_STA | CR_RD2);
```

### process.c

プロセス関連のマジックナンバー（最大プロセス数、スタックサイズ等）を確認し、
定数化されていなければ対応。

### ext3fs.c

```c
// ブロックサイズ関連は既に BLOCK_SIZE (4096) が定義済み
// inodeサイズ等のマジックナンバーがあれば定数化
#define EXT3_INODE_SIZE       128
#define EXT3_DIRECT_BLOCKS    12
#define EXT3_SUPER_BLOCK_OFFSET 1024
```

## 作業手順

### Step 1: マジックナンバーの洗い出し

各ファイルで数値リテラルを検索し、意味が不明なものをリストアップ:

```bash
# 0x で始まるハードコード値を検索
grep -rn '0x[0-9a-fA-F]\+' src/*.c src/drivers/*.c --include='*.c'
```

### Step 2: 優先度付け

- **高**: 複数箇所で使われている値、意味が分からない値
- **中**: 1箇所だが重要な設定値
- **低**: 自明な値（0, 1, NULL等）

### Step 3: 定数定義の配置

| 定数の種類 | 配置場所 |
|-----------|---------|
| カーネル全体 | `src/include/sodex/const.h` |
| メモリ管理固有 | `src/include/memory.h` |
| VGA固有 | `src/include/vga.h` |
| PIC/IDT固有 | `src/include/idt.h` or 新規 `pic.h` |
| ext3固有 | `src/include/ext3fs.h` |
| NE2000固有 | `src/include/ne2000.h`（既に多くは定義済み） |

### Step 4: 置換と確認

1ファイルずつ定数化し、都度ビルド確認:

```bash
# 1ファイル変更後
make clean && make
# QEMUで動作確認
```

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `src/memory.c` | メモリアドレス・サイズの定数化 |
| `src/include/memory.h` | 定数定義追加 |
| `src/idt.c` | PIC・ゲートタイプの定数化 |
| `src/vga.c` | VGA色属性の定数化 |
| `src/include/vga.h` | VGA定数定義追加 |
| `src/drivers/ne2000.c` | 不明な数値を定数に |
| `src/ext3fs.c` | ext3メタデータサイズの定数化 |

## リスク

- 低い。名前の置き換えのみで動作への影響は最小限
- ただし置換ミスで値が変わるリスクあり → ビルド後にQEMUで動作確認必須

## 完了条件

- [ ] 主要な0xで始まる数値リテラルが名前付き定数になっている
- [ ] 各定数にコメントで意味が記載されている
- [ ] `make clean && make` が成功
- [ ] QEMUでカーネルが正常起動する
