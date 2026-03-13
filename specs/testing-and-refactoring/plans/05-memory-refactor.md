# Plan 05: メモリ管理のリファクタリング

## 概要

`src/memory.c` のテスタビリティ改善とコード品質向上のためのリファクタリング。

## 問題点

### 1. init_mem() がハードウェア検出とデータ構造初期化を混在

```c
PUBLIC void init_mem()
{
    // 1. ハードウェアメモリサイズ検出
    // 2. メモリホールリスト構築
    // 3. カーネル使用領域の予約
    // すべてが1つの関数に混在
}
```

### 2. マジックナンバー

```c
#define KERNEL_MEMEND   0xc2000000    // なぜこの値？
#define KERNEL_PMEMBASE 0x2000000     // 物理ベース
#define MIN_MEMSIZE     32            // 最小割り当てサイズ
```

### 3. グローバル変数の直接操作

メモリホールリスト（muse_list, mfree_list, mhole_list）が
関数をまたいで直接操作されている。

### 4. エラーハンドリング

kalloc() が失敗時に NULL を返すが、呼び出し側でチェックされていない箇所がある。

## リファクタリング手順

### Step 1: init_mem() の分割

```c
// ハードウェア非依存のコアロジック
// テストから直接呼び出し可能
PUBLIC void init_mem_core(void *base_addr, u_int32_t size)
{
    // メモリホールリスト初期化
    // base_addr から size バイトを最初のフリーホールとして登録
}

// ハードウェア依存の初期化（既存のinit_mem）
PUBLIC void init_mem(void)
{
    // メモリサイズ検出
    u_int32_t mem_size = detect_memory_size();
    void *base = (void *)KERNEL_MEMEND;

    init_mem_core(base, mem_size);
}
```

### Step 2: マジックナンバーの定数化

```c
// 変更前
#define KERNEL_MEMEND 0xc2000000

// 変更後: 意味を明確にする
#define KERNEL_HEAP_START   0xC2000000  // カーネルヒープ開始（仮想アドレス）
#define KERNEL_HEAP_PSTART  0x02000000  // カーネルヒープ開始（物理アドレス）
#define KALLOC_MIN_SIZE     32          // kalloc最小割り当てサイズ（バイト）
#define MAX_MEMORY_HOLES    1024        // メモリホール最大数
```

これらは `src/include/memory.h` に移動して公開する。

### Step 3: リスト操作の関数化

メモリホールリストの操作を専用関数に分離:

```c
// 現在: 各所でリストを直接操作
mhole->next = mfree_list.next;
mfree_list.next = mhole;
mhole->prev = &mfree_list;
if (mhole->next) mhole->next->prev = mhole;

// 改善: 関数に抽出
PRIVATE void mhole_insert(MemHole *list, MemHole *hole);
PRIVATE void mhole_remove(MemHole *hole);
PRIVATE MemHole *mhole_pool_get(void);
PRIVATE void mhole_pool_return(MemHole *hole);
```

### Step 4: kalloc/kfree のロジック整理

現在の kalloc:
```
1. 空きリスト走査（first-fit）
2. 見つかったブロックのサイズチェック
3. 分割（必要な場合）
4. 使用中リストに移動
5. アドレス返却
```

整理:
```c
PUBLIC void *kalloc(u_int32_t size)
{
    if (size < KALLOC_MIN_SIZE) size = KALLOC_MIN_SIZE;

    MemHole *hole = mhole_find_fit(&mfree_list, size);
    if (!hole) return NULL;

    if (hole->size > size + KALLOC_MIN_SIZE) {
        mhole_split(hole, size);
    }

    mhole_remove(hole);               // 空きリストから外す
    mhole_insert(&muse_list, hole);    // 使用中リストに追加

    return hole->addr;
}
```

### Step 5: _kprint_mem() の改善

デバッグ用のメモリダンプ関数を、テスト時にも使えるよう整理:

```c
PUBLIC void _kprint_mem(void)
{
    _kprintf("=== Memory Status ===\n");
    _kprintf("Free blocks:\n");
    for (MemHole *h = mfree_list.next; h; h = h->next) {
        _kprintf("  addr=0x%x size=%d\n", h->addr, h->size);
    }
    _kprintf("Used blocks:\n");
    for (MemHole *h = muse_list.next; h; h = h->next) {
        _kprintf("  addr=0x%x size=%d\n", h->addr, h->size);
    }
}
```

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `src/memory.c` | init_mem分割、リスト操作関数化、マジックナンバー定数化 |
| `src/include/memory.h` | 定数定義追加、init_mem_core宣言追加 |

## リスク

- リファクタリングでメモリ管理が壊れるとカーネル全体が動かなくなる
- 対策: Plan 04のテストを先に通してから、テスト駆動でリファクタリング

## 依存関係

- Plan 04（メモリテスト）のテストが存在していること
- リファクタリング中は頻繁にテスト実行して回帰チェック

## 完了条件

- [ ] init_mem() が init_mem_core() + ハードウェア検出に分離されている
- [ ] マジックナンバーが名前付き定数になっている
- [ ] リスト操作が専用関数に分離されている
- [ ] Plan 04 のテストが全てPASS
- [ ] `make clean && make` が成功
- [ ] QEMUでカーネルが正常起動し、プロセスが実行できる
