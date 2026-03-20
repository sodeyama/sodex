# 自己修正カーネルアーキテクチャ — 3層設計によるLLM駆動カーネル増強

**執筆日**: 2026-03-20
**前提**: [agent-os-fusion-thesis.md](agent-os-fusion-thesis.md) の設計論考を発展。カーネル自体をLLM修正対象にする構想
**対象**: Sodex — i486向け完全独自OSカーネル

---

## 序論: なぜカーネル自体を修正対象にするのか

[agent-os-fusion-thesis.md](agent-os-fusion-thesis.md) では、LLMエージェントのユーザランド配置が正しい判断であることを確認した。障害隔離、開発速度、メモリ保護の利点は明白だ。

しかし、ユーザランドだけでは到達できない領域がある:

- **スケジューラポリシーの最適化** — ワークロード特性に応じたコンテキストスイッチ戦略の変更
- **メモリアロケータの改善** — 断片化パターンに応じたアルゴリズム切り替え
- **デバイスドライバの修正** — ハードウェアの不具合に対するランタイム回避策
- **ネットワークスタックのチューニング** — 通信パターンに応じたバッファ戦略変更
- **新syscallの追加** — エージェントが必要とする新機能をカーネルに注入

これらはすべてカーネル空間の変更を要求する。従来なら「再コンパイル→再起動」が必要だが、**Core Kernelを固定し、その上のKernel層をホットリロード可能にする**設計で解決できる。

---

## 1. 最新研究の動向

### 1.1 直接関連する研究

| プロジェクト | 発表 | 概要 | Sodexとの関係 |
|-------------|------|------|-------------|
| **LDOS** (UT Austin) | NSF Expeditions 2024-2029, $12M | ML駆動ポリシーが静的カーネル実装を置換。Asterinasカーネルが基盤 | 最も近い思想。ただしLinux ABI互換が前提 |
| **sched_ext** | Linux 6.12 (2024) | eBPFでスケジューラを実行時差し替え。Meta, Google, Valve採用 | Kernel層のスケジューラモジュール化の先行事例 |
| **Kgent** | ACM SIGCOMM 2024 | LLMが自然言語からeBPFプログラムを生成+Z3で検証。正確度80% | LLMコード生成+検証パイプラインの実証 |
| **AutoOS** | ICML 2024 | LLMがLinuxカーネル設定を自律最適化。25%性能向上 | カーネルチューニングの自動化 |
| **AutoVerus** | ICLR 2025 | LLMがRustコード+形式的証明を同時生成 | AI生成カーネルコードの安全性担保 |
| **Composable Kernel** | arXiv 2508.00604 | NeuroSymbolicカーネル設計。LKMをAI計算ユニットとして活用 | カーネルモジュール化のAI向け設計 |
| **KernelGPT** | ASPLOS 2025 | LLMでSyzkaller仕様を自動推論。24バグ発見、12修正 | カーネルコードのLLM理解能力の実証 |

### 1.2 安全性に関する研究

LLM生成コードの安全性は最大の課題である:

- LLM生成コードの **12-65%** にCWE分類の脆弱性が含まれる（バッファオーバーフロー、未チェックリターン等）
- カーネル空間では1つのバグがシステム全体を破壊する
- **AutoVerus** (ICLR 2025) がコード+証明の同時生成で最も有望な解法を示している
- **Kgent** のZ3ベース検証はeBPF限定だが、考え方は汎用化可能

### 1.3 Sodexの独自ポジション

既存研究との決定的な違い:

1. **LDOS**: Asterinas (Rust/Linux ABI互換) が基盤。Sodexは完全独自でPOSIX非準拠
2. **sched_ext**: Linux固有のeBPF基盤に依存。Sodexには存在しない
3. **Composable Kernel**: Linux LKMの拡張。Sodexはゼロから設計可能

**Sodexの利点**: カーネル全体が数千行でLLMのコンテキストに収まる。レガシー互換性の制約がない。AI-nativeな設計を最初から組み込める。

---

## 2. 3層カーネルアーキテクチャ

### 2.1 設計概念

```
┌─────────────────────────────────────────────────────────┐
│                User Land (ユーザーランド層)                │
│   アプリケーション、Agent Transport、シェル                │
│   仮想アドレス: 0x00000000 - 0xBFFFFFFF                  │
│                                                         │
│   修正方法: LLM自由に修正、プロセス再起動で反映            │
├─────────────────────────────────────────────────────────┤
│                Kernel Services (カーネル層)               │
│   スケジューラポリシー、FS実装、ネットワークスタック         │
│   デバイスドライバ、syscallハンドラ群                      │
│   仮想アドレス: 0xC1000000 - 0xC1FFFFFF (16MB)           │
│                                                         │
│   修正方法: LLMが修正→再コンパイル→ホットリロード（再起動不要）│
├─────────────────────────────────────────────────────────┤
│                Core Kernel (コアカーネル層)                │
│   ページング、GDT/IDT、メモリアロケータ基盤               │
│   モジュールローダー、割り込みディスパッチ                  │
│   仮想アドレス: 0xC0001000 - 0xC0FFFFFF (≈16MB)          │
│                                                         │
│   修正方法: 固定・不変。再起動でのみ更新                   │
└─────────────────────────────────────────────────────────┘
```

### 2.2 各層の責務

#### Core Kernel（不変層）

カーネルの存在基盤。いかなる状況でも動作し続けなければならない。

```
Core Kernel 構成:
├── startup.S          — ブートシーケンス、初期ページング
├── gdt.c              — GDTセットアップ
├── idt.c              — IDTセットアップ、割り込みディスパッチ基盤
├── page.c / page_asm.S — ページテーブル操作プリミティブ
├── memory.c           — kalloc/kfree/palloc/pfree
├── pit.c              — タイマー（ウォッチドッグ兼用）
├── serial.c           — 外部LLMとの通信経路
├── module_loader.c    — 【新規】ELFローダー、シンボル解決
├── dispatch.c         — 【新規】ディスパッチテーブル管理
└── rollback.c         — 【新規】モジュールロールバック管理
```

Core Kernelの設計原則:
- **自己完結**: Kernel層がすべてアンロードされてもCoreだけで最低限動作する
- **シンボルエクスポート**: Kernel層が利用できる安定APIを公開する
- **不変性**: 実行時に自身のコード・データは変更されない

#### Kernel Services（可変層）

LLMが修正・差し替え可能な機能モジュール群。

```
Kernel Services モジュール:
├── sched_module.c     — スケジューラポリシー
├── ext3fs_module.c    — ファイルシステム実装
├── syscall_handlers.c — 個別syscallハンドラ群
├── process_module.c   — プロセス管理ポリシー
├── net_module/        — ネットワークスタック
│   ├── uip.c
│   ├── ne2000.c
│   └── socket.c
├── drivers_module/    — デバイスドライバ
│   ├── pci.c
│   ├── uhci.c
│   └── keyboard.c
└── signal_module.c    — シグナルハンドリング
```

各モジュールは以下のインターフェースを実装する:

```c
struct module_info {
    const char *name;
    u_int32_t version;
    int (*init)(void);       // モジュール初期化
    void (*cleanup)(void);   // モジュール解放
    void *ops;               // ディスパッチテーブル（型はモジュール種別による）
};
```

#### User Land（既存層）

現在のAgent Transport、libagent、CLI agentなど。変更なし。

---

## 3. 技術的課題と解法

### 3.1 課題: カーネルコードが絶対アドレスでリンクされている

**現状**: Sodexカーネルは`-fno-pie`でコンパイルされ、`boot.ld.S`で0xC0001000からの絶対アドレスにリンクされる。リロケーション情報は破棄される。

**解法**: Kernel層を Position-Independent Code (PIC) またはリロケータブルオブジェクトとしてコンパイル

```
Core Kernel: -fno-pie（従来通り、固定アドレス0xC0001000〜）
Kernel層:    -fpic -mno-plt で位置独立コードとして生成
             または .o（リロケータブルオブジェクト）のまま保持
```

Core KernelのELFローダーが、Kernel層モジュールのロード時にリロケーションエントリを解決する。

リロケーション処理の具体例:

```c
// module_loader.c (Core Kernel内)

// ELF RELエントリの処理
PRIVATE int apply_relocation(struct elf_module *mod,
                             Elf32_Rel *rel,
                             Elf32_Sym *sym) {
    u_int32_t *target = (u_int32_t *)(mod->load_base + rel->r_offset);
    u_int32_t sym_addr;

    // シンボル解決: Core Kernelのエクスポートテーブルから検索
    if (resolve_symbol(mod->strtab + sym->st_name, &sym_addr) < 0)
        return -1;

    switch (ELF32_R_TYPE(rel->r_info)) {
        case R_386_32:      // 絶対アドレス
            *target += sym_addr;
            break;
        case R_386_PC32:    // PC相対
            *target += sym_addr - (u_int32_t)target;
            break;
        default:
            return -1;  // 未対応リロケーション
    }
    return 0;
}
```

### 3.2 課題: 実行時に新しいコードページをマップする手段が限定的

**現状**: `pg_set_kernel_4m_page()`は4MB PSEページ単位でしかマップできない。BGAフレームバッファ用にのみ使用。

**解法**: 4KBページ単位のカーネルマッピング関数を追加

```c
// page.c に追加 (Core Kernel)

// Kernel Services領域 (0xC1000000-0xC1FFFFFF) 用のページテーブル
PRIVATE u_int32_t ks_page_tables[16][1024] __attribute__((aligned(4096)));

// 4KBページ単位でカーネル仮想アドレスにマッピング
PUBLIC int pg_map_kernel_4k_page(u_int32_t vaddr, u_int32_t paddr,
                                  u_int32_t flags) {
    u_int32_t pde_idx = vaddr >> 22;
    u_int32_t pte_idx = (vaddr >> 12) & 0x3FF;

    // Kernel Services領域のみ許可
    if (pde_idx < 772 || pde_idx >= 788)  // 0xC1000000-0xC1FFFFFF
        return -1;

    u_int32_t tbl_idx = pde_idx - 772;

    // PDEが未設定なら設定
    if (!(pg_dir[pde_idx] & PAGE_PRESENT)) {
        u_int32_t tbl_phys = (u_int32_t)&ks_page_tables[tbl_idx]
                             - __PAGE_OFFSET;
        pg_dir[pde_idx] = tbl_phys | PAGE_PRESENT | PAGE_RW;
    }

    // PTEを設定
    ks_page_tables[tbl_idx][pte_idx] = paddr | flags;
    pg_flush_tlb();

    return 0;
}

// マッピング解除
PUBLIC int pg_unmap_kernel_4k_page(u_int32_t vaddr) {
    u_int32_t pde_idx = vaddr >> 22;
    u_int32_t pte_idx = (vaddr >> 12) & 0x3FF;
    u_int32_t tbl_idx = pde_idx - 772;

    if (pde_idx < 772 || pde_idx >= 788)
        return -1;

    ks_page_tables[tbl_idx][pte_idx] = 0;
    // invlpg相当（TLBの該当エントリのみ無効化）
    pg_flush_tlb();

    return 0;
}
```

### 3.3 課題: シンボル解決の仕組みがない

**現状**: モノリシックカーネルのため、すべてのシンボルはリンク時に解決される。

**解法**: Core Kernelにシンボルテーブルを組み込む

```c
// dispatch.c (Core Kernel)

struct kernel_symbol {
    const char *name;
    void *addr;
};

// Core Kernelがエクスポートするシンボル
PRIVATE struct kernel_symbol export_table[] = {
    // メモリ管理
    { "kalloc",           (void *)kalloc },
    { "kfree",            (void *)kfree },
    { "palloc",           (void *)palloc },
    { "pfree",            (void *)pfree },

    // 画面・シリアル出力
    { "printk",           (void *)printk },
    { "serial_write",     (void *)serial_write },

    // 割り込み管理
    { "set_intr_gate",    (void *)set_intr_gate },
    { "set_trap_gate",    (void *)set_trap_gate },
    { "cli",              (void *)cli },
    { "sti",              (void *)sti },

    // ページング
    { "pg_map_kernel_4k_page",   (void *)pg_map_kernel_4k_page },
    { "pg_unmap_kernel_4k_page", (void *)pg_unmap_kernel_4k_page },
    { "pg_flush_tlb",            (void *)pg_flush_tlb },

    // ディスパッチテーブル登録
    { "register_sched_ops",      (void *)register_sched_ops },
    { "register_syscall_handler",(void *)register_syscall_handler },
    { "register_fs_ops",         (void *)register_fs_ops },

    { NULL, NULL }  // 終端
};

PUBLIC int resolve_symbol(const char *name, void **addr) {
    for (int i = 0; export_table[i].name != NULL; i++) {
        if (kstrcmp(name, export_table[i].name) == 0) {
            *addr = export_table[i].addr;
            return 0;
        }
    }
    return -1;  // 未解決シンボル
}
```

### 3.4 課題: ディスパッチの切り替え中にレースコンディションが発生する

**現状**: syscallはswitch文、スケジューラは`schedule()`関数が直接呼ばれる。モジュール差し替え中に呼び出されると不整合が起きる。

**解法**: 関数ポインタによるアトミックなディスパッチ切り替え

```c
// dispatch.c (Core Kernel)

// --- スケジューラディスパッチ ---
struct sched_ops {
    struct task_struct* (*pick_next)(void);
    void (*enqueue)(struct task_struct *);
    void (*dequeue)(struct task_struct *);
    void (*tick)(void);
};

PRIVATE volatile struct sched_ops *current_sched = &default_sched_ops;

// Core Kernelのschedule() — この関数自体は変わらない
PUBLIC void schedule(void) {
    struct sched_ops *ops = (struct sched_ops *)current_sched;
    struct task_struct *next = ops->pick_next();
    if (next && next != current_task)
        context_switch(next);
}

// モジュールがスケジューラを登録
PUBLIC void register_sched_ops(struct sched_ops *ops) {
    // 割り込み禁止してポインタを切り替え（i486ではポインタ書き込みはアトミック）
    u_int32_t flags = cli_save();
    current_sched = ops;
    sti_restore(flags);
}

// --- syscallディスパッチ ---
typedef int (*syscall_handler_t)(u_int32_t, u_int32_t, u_int32_t,
                                  u_int32_t, u_int32_t);

PRIVATE volatile syscall_handler_t syscall_table[NR_SYSCALLS];

// Core Kernelのsyscallエントリ — この関数自体は変わらない
PUBLIC int dispatch_syscall(int nr, u_int32_t a, u_int32_t b,
                            u_int32_t c, u_int32_t d, u_int32_t e) {
    if (nr < 0 || nr >= NR_SYSCALLS || !syscall_table[nr])
        return -ENOSYS;
    return syscall_table[nr](a, b, c, d, e);
}

// モジュールがsyscallハンドラを登録
PUBLIC void register_syscall_handler(int nr, syscall_handler_t handler) {
    if (nr >= 0 && nr < NR_SYSCALLS) {
        u_int32_t flags = cli_save();
        syscall_table[nr] = handler;
        sti_restore(flags);
    }
}
```

---

## 4. ホットリロードシーケンス

### 4.1 正常フロー

```
LLM Agent (外部 / ユーザランド)
  │
  │ ① カーネルログ・メトリクス解析
  │ ② 改善対象モジュールの特定
  │ ③ 既存ソースコードの理解（カーネル全体がコンテキストに収まる）
  │ ④ 新しいCソースコード生成
  │ ⑤ クロスコンパイル: x86_64-elf-gcc -m32 -fpic -nostdlib -c module.c -o module.o
  │ ⑥ 静的検証（後述 §5）
  │ ⑦ ELFオブジェクトをシリアル / 共有メモリ経由で転送
  │
  ▼
Core Kernel: module_loader
  │
  │ ⑧  受信したELFの整合性チェック（マジックナンバー、アーキテクチャ）
  │ ⑨  必要メモリサイズ算出、Kernel Services領域からkalloc
  │ ⑩  .text, .data, .rodata, .bssセクションをロード
  │ ⑪  リロケーション適用（Core Kernelシンボルを解決）
  │ ⑫  未解決シンボルがあれば中止→エラー報告
  │ ⑬  module_info構造体を取得、バージョンチェック
  │ ⑭  旧モジュールのcleanup()呼び出し
  │ ⑮  ディスパッチポインタをアトミックに切り替え（cli/sti保護下）
  │ ⑯  新モジュールのinit()呼び出し
  │ ⑰  旧モジュールのメモリ解放
  │ ⑱  ロード結果をシリアル経由でLLMに報告
  │
  ▼
新Kernel Serviceモジュールが稼働（再起動なし）
```

### 4.2 ロールバックフロー

```
正常稼働中
  │
  │ ウォッチドッグタイマー発火 / panic検出 / LLMからのロールバック指示
  │
  ▼
Core Kernel: rollback manager
  │
  │ ① 割り込み禁止
  │ ② 現モジュールのcleanup()呼び出し（可能なら）
  │ ③ 前バージョンのモジュールのinit()呼び出し
  │ ④ ディスパッチポインタを前バージョンに復帰
  │ ⑤ 割り込み再開
  │ ⑥ 失敗モジュールのメモリ解放
  │ ⑦ ロールバック結果をシリアル経由でLLMに報告
  │
  ▼
前バージョンで継続稼働
```

設計上、**常に1つ前のバージョンをメモリに保持**する。これにより即座にロールバック可能。

---

## 5. 安全性アーキテクチャ

### 5.1 3段階防御

```
┌─────────────────────────────────────────────┐
│  Level 3: LLM側事前検証（外部）               │
│  ─────────────────────────────────────────── │
│  • コード生成時の安全パターン適用              │
│  • 静的解析ツール実行                         │
│  • QEMU上でのドライラン（テストハーネス）       │
│  • 前バージョンとのdiff分析                    │
├─────────────────────────────────────────────┤
│  Level 2: Core Kernelロード時検証              │
│  ─────────────────────────────────────────── │
│  • ELFフォーマット整合性                       │
│  • 全シンボルの解決可否                        │
│  • メモリアクセス範囲チェック                   │
│    （Kernel Services領域外への参照を禁止）      │
│  • コードサイズ上限チェック                     │
│  • module_info構造体の存在確認                  │
├─────────────────────────────────────────────┤
│  Level 1: 実行時保護                          │
│  ─────────────────────────────────────────── │
│  • Kernel Servicesページ → Core領域への        │
│    書き込み権限なし（ページテーブルで強制）       │
│  • ウォッチドッグタイマー                      │
│    （一定時間応答なし → 自動ロールバック）       │
│  • panic時の自動ロールバック                    │
│  • 前バージョンの常時保持                      │
└─────────────────────────────────────────────┘
```

### 5.2 ページテーブルによるCore Kernel保護

Kernel Servicesモジュールのコードページに対して、Core Kernel領域(0xC0000000-0xC0FFFFFF)への書き込みを禁止する:

```c
// Core Kernel領域のページテーブルエントリ
// PAGE_PRESENT | PAGE_GLOBAL （PAGE_RWなし = 読み取り専用）
// ※ Kernel Services内のコードからはCore Kernelのデータを読めるが書けない
// ※ Core Kernel自身のコードからは書き込み可能（CR0.WPの制御）
```

ただしi486のページ保護はスーパーバイザモードでは限定的（CR0.WPが必要）であり、完全な保護にはソフトウェア検証との併用が必要。

### 5.3 ウォッチドッグによる異常検出

```c
// Core Kernel: pit.c に追加

// ウォッチドッグタイマー（PITの既存タイマー割り込みに統合）
PRIVATE volatile u_int32_t watchdog_counter = 0;
PRIVATE volatile u_int32_t watchdog_limit = 1000;  // 10秒 (10ms × 1000)

// タイマー割り込みハンドラ内で呼ばれる
PUBLIC void watchdog_tick(void) {
    watchdog_counter++;
    if (watchdog_counter >= watchdog_limit) {
        // Kernel Serviceモジュールがハングしていると判断
        trigger_module_rollback();
    }
}

// Kernel Serviceモジュールが定期的に呼ぶ（ハートビート）
PUBLIC void watchdog_reset(void) {
    watchdog_counter = 0;
}
```

---

## 6. Sodex固有のメモリレイアウト設計

### 6.1 現状のメモリレイアウト

```
仮想アドレス空間:
0x00000000 ─┬─ ユーザー空間
             │  PDE[0-767]
0xBFFFFFFF ─┘
0xC0000000 ─┬─ カーネル空間開始 (__PAGE_OFFSET)
0xC0001000 ─┤─ カーネルコード開始 (__KERNEL_START)
             │  .text, .rodata, .data, .bss
0xC0100000 ─┤─ BSS (pg_dir等)
             │
0xC2000000 ─┤─ カーネルヒープ上限 (KERNEL_HEAP_VEND)
             │  （未使用領域）
0xFFFFFFFF ─┘

物理メモリ:
0x00000000 ─┬─ 予約（ブートローダ等）
0x00001000 ─┤─ カーネル物理配置
0x00100000 ─┤─ BSS物理配置
0x02000000 ─┤─ プロセスメモリプール開始
0x04000000 ─┘─ プロセスメモリプール終了
```

### 6.2 提案: 3層対応メモリレイアウト

```
仮想アドレス空間:
0x00000000 ─┬─ ユーザーランド層
             │  PDE[0-767]
0xBFFFFFFF ─┘
0xC0000000 ─┬─ Core Kernel（固定）
0xC0001000 ─┤─ Core .text/.rodata/.data/.bss
             │  シンボルテーブル、ディスパッチテーブル
             │  モジュールローダー、ロールバック管理
0xC0FFFFFF ─┘─ Core Kernel上限 (16MB)
0xC1000000 ─┬─ Kernel Services（ホットリロード可能）
             │  PDE[772-787]: 4KBページテーブルで管理
             │  モジュール毎にページ単位で割り当て
             │  ┌── sched_module    (数ページ)
             │  ├── fs_module       (数十ページ)
             │  ├── net_module      (数十ページ)
             │  ├── driver_modules  (数十ページ)
             │  ├── syscall_module  (数ページ)
             │  └── [前バージョン保持領域]
0xC1FFFFFF ─┘─ Kernel Services上限 (16MB)
0xC2000000 ─┬─ カーネルヒープ（kallocが管理）
             │  Core / Kernel Services共用
0xC2FFFFFF ─┘
```

PDE配置:
- PDE[768-771]: Core Kernel (4 × 4MB PSE = 16MB)
- PDE[772-787]: Kernel Services (16 × 4MB = 64MB、4KBページテーブルで細粒度管理)
- PDE[788-799]: カーネルヒープ

### 6.3 リンカスクリプトの分離

```
// core.ld.S — Core Kernel用
SECTIONS {
    . = __KERNEL_START;  /* 0xC0001000 */
    .text : AT(ADDR(.text) - __PAGE_OFFSET) {
        /* Core Kernelコードのみ */
    }
    /* ... */
    . = 0xC0FFFFFF;
    __core_end = .;
}

// module.ld.S — Kernel Servicesモジュール用（リロケータブル）
SECTIONS {
    /* ベースアドレスなし — ローダーが決定 */
    .text : { *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : { *(.bss) }
    .module_info : { *(.module_info) }
}
```

---

## 7. LLMによるカーネル修正パイプライン

### 7.1 エンドツーエンドフロー

```
┌──────────────────────────────────────────────────┐
│ LLM Agent (Claude API)                           │
│                                                  │
│  ① Monitor: シリアル経由でカーネルログ受信         │
│     - printk出力、パフォーマンスカウンタ            │
│     - ウォッチドッグ発火履歴                       │
│     - モジュールロード/アンロード履歴               │
│                                                  │
│  ② Analyze: 問題・改善機会を特定                   │
│     - スケジューラのコンテキストスイッチ頻度が高い   │
│     - メモリ断片化率が閾値を超えている              │
│     - 特定syscallのレイテンシが異常                │
│                                                  │
│  ③ Plan: 修正計画を立案                           │
│     - 影響範囲の特定                              │
│     - 依存モジュールの確認                         │
│     - ロールバック条件の設定                       │
│                                                  │
│  ④ Generate: Cソースコード生成                    │
│     - module_info構造体を含む                     │
│     - Core Kernelのエクスポートシンボルのみ使用     │
│     - 前バージョンとのAPI互換性を維持               │
│                                                  │
│  ⑤ Verify: 事前検証                              │
│     - x86_64-elf-gccでコンパイルエラーチェック      │
│     - 静的解析（バッファオーバーフロー等）           │
│     - QEMUテストハーネスでのドライラン              │
│                                                  │
│  ⑥ Deploy: モジュール転送+ロード指示               │
│     - ELFバイナリをシリアル/共有メモリで転送         │
│     - Core Kernelにロードコマンド送信               │
│                                                  │
│  ⑦ Observe: ロード後の挙動監視                    │
│     - 異常検出時は即座にロールバック指示             │
│     - 改善効果の定量評価                           │
└──────────────────────────────────────────────────┘
```

### 7.2 モジュール転送プロトコル

シリアルポート経由のシンプルなプロトコル:

```
// フレーム構造
┌─────────┬──────┬────────┬──────────┬──────────┐
│ MAGIC   │ CMD  │ LENGTH │ PAYLOAD  │ CHECKSUM │
│ 0xCAFE  │ 1B   │ 4B     │ N bytes  │ CRC32    │
└─────────┴──────┴────────┴──────────┴──────────┘

CMD:
  0x01 = MODULE_LOAD     (payload = ELFバイナリ)
  0x02 = MODULE_UNLOAD   (payload = モジュール名)
  0x03 = MODULE_LIST     (payload = なし、応答にモジュール一覧)
  0x04 = MODULE_ROLLBACK (payload = モジュール名)
  0x05 = KERNEL_STATUS   (payload = なし、応答にカーネル状態)
```

---

## 8. 実装ロードマップ

### Phase 0: 基盤整備（前提条件）

- [ ] syscall.cのswitch文を関数ポインタテーブルに変換
- [ ] スケジューラをstruct sched_opsパターンに分離
- [ ] printk/シリアル出力の統一

### Phase 1: Core Kernel分離

- [ ] リンカスクリプト分離（core.ld.S + module.ld.S）
- [ ] Core Kernelシンボルテーブル実装
- [ ] Kernel Services用4KBページマッピング関数追加
- [ ] 基本的なELF .oローダー実装

### Phase 2: モジュールホットリロード

- [ ] module_info構造体とモジュールインターフェース定義
- [ ] ディスパッチテーブルのアトミック切り替え実装
- [ ] 1つ目のモジュール: スケジューラを分離してリロード可能に
- [ ] ロールバック機構実装

### Phase 3: 転送と制御

- [ ] シリアル経由モジュール転送プロトコル実装
- [ ] Core Kernelコマンドインタープリタ
- [ ] ウォッチドッグタイマー統合

### Phase 4: LLM統合

- [ ] 外部LLMエージェントからのモジュール転送パイプライン
- [ ] カーネルメトリクス収集・シリアル出力
- [ ] LLMによるモジュール生成→コンパイル→転送→ロードの自動化
- [ ] 異常検出→ロールバック→再生成のフィードバックループ

---

## 9. 既存研究との差別化

| 特性 | LDOS | sched_ext | LKM (Linux) | **Sodex 3層** |
|------|------|-----------|-------------|---------------|
| 基盤カーネル | Asterinas (Rust) | Linux | Linux | **独自 (C/asm)** |
| ABI互換性 | Linux ABI | Linux | Linux | **なし（自由設計）** |
| 修正対象 | ポリシーパラメータ | スケジューラのみ | 任意モジュール | **任意モジュール** |
| 修正方法 | ML推論 | eBPFバイトコード | Cコンパイル済み | **LLM生成Cコード** |
| 安全性機構 | 型安全 (Rust) | eBPF検証器 | なし（危険） | **3段階防御** |
| LLM統合 | 間接的 | なし | なし | **ネイティブ** |
| カーネルサイズ | 中規模 | 巨大 | 巨大 | **極小（数千行）** |
| LLMの理解容易性 | 低 | 低 | 低 | **高（全体把握可能）** |

Sodexの決定的優位点: **カーネル全体がLLMのコンテキストウィンドウに収まる**。LLMが修正対象のコードだけでなく、それが依存するCore Kernelのコードも同時に理解した上でコードを生成できる。Linux（3000万行）やAsterinas（数万行）ではこれは不可能。

---

## 10. 未解決課題

### 10.1 状態移行問題

モジュール切り替え時に、旧モジュールが保持していた内部状態（例: スケジューラのランキュー、ファイルシステムのキャッシュ）をどう新モジュールに引き継ぐか。

考えられるアプローチ:
- **状態のシリアライズ/デシリアライズ**: module_infoにserialize/deserialize関数を追加
- **状態をCore Kernelに持つ**: ランキューなどの基本データ構造はCore側に配置し、ポリシーのみモジュール化
- **状態リセット許容**: 一部の状態は失ってもよいと割り切る（スケジューラキューは自然に再構築される）

### 10.2 モジュール間依存

ネットワークモジュールがファイルシステムモジュールを呼ぶ場合など、Kernel Servicesモジュール間の依存関係の解決方法。

考えられるアプローチ:
- **すべてのモジュール間呼び出しはCore Kernelのディスパッチテーブル経由**: 直接参照を禁止
- **依存グラフ管理**: Core Kernelが依存関係を追跡し、依存先の先にロードを強制

### 10.3 割り込みコンテキストでのモジュール切り替え

タイマー割り込みハンドラ内でスケジューラモジュールが呼ばれている最中にモジュール切り替えは安全か。

解法: **割り込み禁止区間でのみ切り替えを実行**。ディスパッチポインタの書き換えはアトミックだが、旧モジュールのcleanup→新モジュールのinitの間は割り込みを禁止する。

---

## 参考文献

### 直接関連研究
- [LDOS: Learning-Directed Operating System](https://ldos.utexas.edu/) — NSF Expeditions 2024-2029
- [sched_ext: BPF extensible scheduler class](https://www.kernel.org/doc/html/next/scheduler/sched-ext.html) — Linux 6.12
- [Kgent: LLM-Automated eBPF Generation](https://dl.acm.org/doi/10.1145/3672197.3673434) — ACM SIGCOMM 2024
- [AutoOS: LLM-based OS Kernel Configuration Optimization](https://github.com/xuewuyinhe/AutoOS) — ICML 2024
- [KernelGPT: Enhanced Kernel Fuzzing via LLMs](https://github.com/ise-uiuc/KernelGPT) — ASPLOS 2025
- [Composable OS Kernel Architectures for Autonomous Intelligence](https://arxiv.org/abs/2508.00604) — arXiv 2025

### 安全性・検証
- [AutoVerus: LLM-Driven Proof Generation for Rust](https://arxiv.org/pdf/2409.13082) — ICLR 2025
- [Verus: SMT-based Verification for Rust](https://verus-lang.github.io/event-sites/2024-sosp/) — SOSP 2024
- [Asterinas + Verus: Formal Verification for General-Purpose OS](https://asterinas.github.io/2025/02/13/towards-practical-formal-verification-for-a-general-purpose-os-in-rust.html)
- [AI Will Make Formal Verification Mainstream](https://martin.kleppmann.com/2025/12/08/ai-formal-verification.html) — Martin Kleppmann

### カーネルコード生成
- [Towards Automated Kernel Generation in the Era of LLMs](https://arxiv.org/pdf/2601.15727) — Survey, 2026
- [Self-Tuning Linux Kernels via LLM-Driven Agents](https://www.linuxjournal.com/content/self-tuning-linux-kernels-how-llm-driven-agents-are-reinventing-scheduler-policies) — Linux Journal
- [LLM Agents for Always-On OS Tuning](https://liargkovas.com/assets/pdf/Liargkovas_ML4Sys_NeurIPS25.pdf) — NeurIPS ML4Sys 2025

### 先行Sodexドキュメント
- [agent-os-fusion-thesis.md](agent-os-fusion-thesis.md) — エージェント融合のあるべき姿
- [llm-agent-os-survey.md](llm-agent-os-survey.md) — LLM Agent OS調査レポート
- [autonomous_os_architecture_report.md](autonomous_os_architecture_report.md) — 自律OSアーキテクチャ報告
