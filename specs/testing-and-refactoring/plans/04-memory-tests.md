# Plan 04: メモリ管理のユニットテスト

## 概要

`src/memory.c` のカーネルメモリアロケータ（kalloc/kfree/aalloc/afree/palloc/pfree）の
ユニットテストを作成する。

## テスト対象

### memory.c の公開関数

| 関数 | 用途 | 備考 |
|------|------|------|
| `init_mem()` | メモリ管理初期化 | メモリホールリスト構築 |
| `kalloc(size)` | カーネルメモリ割り当て | |
| `kfree(ptr)` | カーネルメモリ解放 | |
| `aalloc(size, align)` | アラインメント付き割り当て | |
| `afree(ptr)` | アラインメント付き解放 | |
| `palloc(size)` | ページアラインメント割り当て | 4KBアラインメント |
| `pfree(ptr)` | ページ割り当て解放 | |
| `get_realaddr(virt)` | 仮想→物理アドレス変換 | __PAGE_OFFSET依存 |
| `_kprint_mem()` | メモリ状態表示 | デバッグ用 |

### 内部データ構造

```c
// メモリホール（空きブロック管理）
typedef struct _MemHole {
    void* addr;
    u_int32_t size;
    struct _MemHole* prev;
    struct _MemHole* next;
} MemHole;

// グローバルリスト
MemHole memhole[MAX_MHOLES];    // 静的配列（MemHoleプール）
MemHole muse_list;               // 使用中リスト
MemHole mfree_list;              // 空きリスト
MemHole mhole_list;              // 未使用MemHoleプール
```

### アロケーションアルゴリズム

- **First-fit**: 空きリストを先頭から走査し、十分なサイズのブロックを見つける
- **分割**: 要求サイズより大きいブロックは分割して残りを空きリストに戻す
- **結合**: 解放時に隣接する空きブロックを結合（フラグメンテーション対策）

## ホスト側テストの課題

### 仮想アドレスの扱い

memory.c は `__PAGE_OFFSET`（0xC0000000）を使った仮想/物理アドレス変換を行う。
テスト時は `__PAGE_OFFSET = 0` として、ホストのmallocで確保した領域をメモリプールとして使う。

### init_mem() のモック化

実際の `init_mem()` はハードウェアのメモリサイズを検出し、カーネルの物理メモリ領域を
ベースにホールリストを構築する。テスト用には別の初期化関数を用意する。

```c
// テスト用: 指定したバッファをメモリプールとして初期化
void test_init_mem(void *pool, u_int32_t pool_size);
```

## テストケース

### test_memory.c

```c
#include "test_framework.h"
#include "mock_kernel.h"
#include "mock_vga.h"

// memory.c をテスト用にコンパイルする際の設定
// TEST_BUILD フラグで init_mem() をテスト版に切り替え

#define POOL_SIZE (1024 * 1024)  // 1MB テスト用プール
static char memory_pool[POOL_SIZE];

// テスト前に毎回呼ぶ初期化
static void setup(void) {
    // memory.c のグローバル状態をリセット
    // test_init_mem(memory_pool, POOL_SIZE);
}

// === 基本的な割り当てと解放 ===

TEST(kalloc_basic) {
    setup();
    void *p = kalloc(100);
    ASSERT_NOT_NULL(p);
}

TEST(kalloc_returns_different_addresses) {
    setup();
    void *p1 = kalloc(100);
    void *p2 = kalloc(100);
    ASSERT_NOT_NULL(p1);
    ASSERT_NOT_NULL(p2);
    ASSERT(p1 != p2);
}

TEST(kfree_basic) {
    setup();
    void *p = kalloc(100);
    ASSERT_NOT_NULL(p);
    int ret = kfree(p);
    ASSERT_EQ(ret, 0);  // 成功
}

TEST(kalloc_after_free) {
    setup();
    void *p1 = kalloc(100);
    kfree(p1);
    void *p2 = kalloc(100);
    ASSERT_NOT_NULL(p2);
    // 解放した領域が再利用されること
}

// === サイズバリエーション ===

TEST(kalloc_zero_size) {
    setup();
    void *p = kalloc(0);
    // 0バイト割り当ての動作を確認（NULLまたは最小割り当て）
}

TEST(kalloc_large) {
    setup();
    void *p = kalloc(POOL_SIZE / 2);
    ASSERT_NOT_NULL(p);
}

TEST(kalloc_too_large) {
    setup();
    void *p = kalloc(POOL_SIZE * 2);
    ASSERT_NULL(p);  // プールより大きい要求は失敗
}

TEST(kalloc_min_size) {
    setup();
    // MIN_MEMSIZE（32バイト）未満の要求が32に切り上げられるか確認
    void *p = kalloc(1);
    ASSERT_NOT_NULL(p);
}

// === 連続割り当て（フラグメンテーション） ===

TEST(kalloc_many_small) {
    setup();
    void *ptrs[100];
    int i;
    for (i = 0; i < 100; i++) {
        ptrs[i] = kalloc(64);
        if (ptrs[i] == NULL) break;
    }
    ASSERT(i > 50);  // 少なくとも50個は割り当てられるはず
}

TEST(free_and_coalesce) {
    setup();
    // 3つの連続ブロックを割り当て
    void *p1 = kalloc(100);
    void *p2 = kalloc(100);
    void *p3 = kalloc(100);

    // 真ん中を解放
    kfree(p2);
    // 両端も解放 → 3つが結合されるはず
    kfree(p1);
    kfree(p3);

    // 結合後、大きなブロックが割り当て可能
    void *p4 = kalloc(280);
    ASSERT_NOT_NULL(p4);
}

// === アラインメント付き割り当て ===

TEST(aalloc_alignment) {
    setup();
    // 4KB（12ビット）アラインメント
    void *p = aalloc(100, 12);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ((u_int32_t)(uintptr_t)p % 4096, 0);
}

TEST(palloc_page_aligned) {
    setup();
    void *p = palloc(8192);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ((u_int32_t)(uintptr_t)p % 4096, 0);
}

// === エッジケース ===

TEST(kfree_null) {
    setup();
    // NULLの解放がクラッシュしないこと
    int ret = kfree(NULL);
    // 戻り値は実装依存（エラーコードまたはクラッシュしなければOK）
}

TEST(double_free) {
    setup();
    void *p = kalloc(100);
    kfree(p);
    // 二重解放の動作を確認（クラッシュしないことが最低条件）
    int ret = kfree(p);
    // 理想的にはエラーを返す
}

// === main ===

int main(void)
{
    printf("=== memory.c tests ===\n");

    RUN_TEST(kalloc_basic);
    RUN_TEST(kalloc_returns_different_addresses);
    RUN_TEST(kfree_basic);
    RUN_TEST(kalloc_after_free);
    RUN_TEST(kalloc_zero_size);
    RUN_TEST(kalloc_large);
    RUN_TEST(kalloc_too_large);
    RUN_TEST(kalloc_min_size);
    RUN_TEST(kalloc_many_small);
    RUN_TEST(free_and_coalesce);
    RUN_TEST(aalloc_alignment);
    RUN_TEST(palloc_page_aligned);
    RUN_TEST(kfree_null);
    RUN_TEST(double_free);

    TEST_REPORT();
}
```

## 実装上の課題

### memory.c のテスト用ビルド

memory.c はグローバル状態と `init_mem()` のハードウェア依存（メモリサイズ検出）が障害。

対策:
1. `TEST_BUILD` 時に `test_init_mem()` を提供し、ホストのバッファをプールとして使う
2. `__PAGE_OFFSET` を0に定義してアドレス変換を無効化
3. `_kprintf` はモックで `printf` にリダイレクト

```c
// memory.c に追加（条件コンパイル）
#ifdef TEST_BUILD
PUBLIC void test_init_mem(void *pool, u_int32_t pool_size)
{
    // mhole_list, mfree_list, muse_list を初期化
    // pool を最初のフリーホールとして登録
}
#endif
```

### init_mem() の分離検討

`init_mem()` を2つに分割するリファクタリング（Plan 05）:
1. `init_mem_core()` — ホールリスト初期化（ハードウェア非依存）
2. `init_mem()` — メモリサイズ検出 + `init_mem_core()` 呼び出し

テストでは `init_mem_core()` だけ呼ぶ。

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `tests/test_memory.c` | 新規。メモリ管理テスト |
| `src/memory.c` | `#ifdef TEST_BUILD` でテスト用初期化関数追加（最小限） |

## 依存関係

- Plan 01（テストフレームワーク）
- Plan 05（memory リファクタリング）と並行可能だが、テスト用初期化の追加が必要

## 完了条件

- [ ] kalloc/kfree の基本テストがPASS
- [ ] 連続割り当て・解放のテストがPASS
- [ ] アラインメント付き割り当てのテストがPASS
- [ ] エッジケース（NULL解放、二重解放）がクラッシュしない
- [ ] フラグメンテーション/結合のテストがPASS
