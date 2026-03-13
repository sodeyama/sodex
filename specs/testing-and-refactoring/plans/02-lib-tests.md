# Plan 02: src/lib/ のユニットテスト

## 概要

`src/lib/` の文字列操作関数、数学関数、メモリブロック管理のユニットテストを作成する。
テスト対象として最も容易で、テストフレームワークの動作確認も兼ねる。

## テスト対象

### lib.c（79行）の関数

| 関数 | シグネチャ | 備考 |
|------|-----------|------|
| `strlen` | `int strlen(const char *s)` | 戻り値がint（size_t でない） |
| `strcmp` | `int strcmp(const char *s1, const char *s2)` | |
| `strncmp` | `int strncmp(const char *s1, const char *s2, int n)` | nがint |
| `strcpy` | `char* strcpy(char *dst, const char *src)` | |
| `strncpy` | `char* strncpy(char *dst, const char *src, int n)` | |
| `strchr` | `char* strchr(const char *s, int c)` | |
| `strrchr` | `char* strrchr(const char *s, int c)` | |
| `pow` | `double pow(double base, double exp)` | 独自実装 |
| `log` | `double log(double x)` | 独自実装 |

### string.c（87行）の関数

lib.c と重複する関数が多い（Plan 03 で統合予定）。
テストは lib.c の関数に対して書き、string.c は統合後にテスト対象から外す。

### memb.c（104行）の関数

| 関数 | シグネチャ | 備考 |
|------|-----------|------|
| `memb_init` | `void memb_init(struct memb *m)` | ブロックプール初期化 |
| `memb_alloc` | `void *memb_alloc(struct memb *m)` | ブロック割り当て |
| `memb_free` | `char memb_free(struct memb *m, void *ptr)` | ブロック解放 |

## テストケース

### test_lib.c — 文字列関数テスト

```c
#include "test_framework.h"

// モック
#include "mock_kernel.h"

// テスト対象（直接コンパイル or リンク）
// gcc test_lib.c ../src/lib/lib.c -o test_lib

// === strlen ===

TEST(strlen_empty) {
    ASSERT_EQ(strlen(""), 0);
}

TEST(strlen_hello) {
    ASSERT_EQ(strlen("hello"), 5);
}

TEST(strlen_single_char) {
    ASSERT_EQ(strlen("x"), 1);
}

TEST(strlen_with_spaces) {
    ASSERT_EQ(strlen("hello world"), 11);
}

// === strcmp ===

TEST(strcmp_equal) {
    ASSERT_EQ(strcmp("abc", "abc"), 0);
}

TEST(strcmp_less) {
    ASSERT(strcmp("abc", "abd") < 0);
}

TEST(strcmp_greater) {
    ASSERT(strcmp("abd", "abc") > 0);
}

TEST(strcmp_empty_strings) {
    ASSERT_EQ(strcmp("", ""), 0);
}

TEST(strcmp_first_empty) {
    ASSERT(strcmp("", "a") < 0);
}

TEST(strcmp_second_empty) {
    ASSERT(strcmp("a", "") > 0);
}

TEST(strcmp_prefix) {
    ASSERT(strcmp("abc", "abcd") < 0);
}

// === strncmp ===

TEST(strncmp_equal_within_n) {
    ASSERT_EQ(strncmp("abcdef", "abcxyz", 3), 0);
}

TEST(strncmp_diff_within_n) {
    ASSERT(strncmp("abcdef", "abxyz", 3) != 0);
}

TEST(strncmp_zero_n) {
    ASSERT_EQ(strncmp("abc", "xyz", 0), 0);
}

// === strcpy ===

TEST(strcpy_basic) {
    char dst[16];
    strcpy(dst, "hello");
    ASSERT_STR_EQ(dst, "hello");
}

TEST(strcpy_empty) {
    char dst[16] = "old";
    strcpy(dst, "");
    ASSERT_STR_EQ(dst, "");
}

// === strncpy ===

TEST(strncpy_basic) {
    char dst[16] = {0};
    strncpy(dst, "hello", 5);
    ASSERT_STR_EQ(dst, "hello");
}

TEST(strncpy_truncate) {
    char dst[4] = {0};
    strncpy(dst, "hello", 3);
    ASSERT(dst[0] == 'h');
    ASSERT(dst[1] == 'e');
    ASSERT(dst[2] == 'l');
}

// === strchr ===

TEST(strchr_found) {
    const char *s = "hello";
    char *p = strchr(s, 'l');
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(*p, 'l');
    ASSERT_EQ(p - s, 2);
}

TEST(strchr_not_found) {
    ASSERT_NULL(strchr("hello", 'z'));
}

TEST(strchr_first_char) {
    const char *s = "hello";
    ASSERT_EQ(strchr(s, 'h'), s);
}

// === strrchr ===

TEST(strrchr_found_last) {
    const char *s = "hello";
    char *p = strrchr(s, 'l');
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p - s, 3);  // 最後の 'l'
}

TEST(strrchr_not_found) {
    ASSERT_NULL(strrchr("hello", 'z'));
}

// === pow ===

TEST(pow_basic) {
    ASSERT(pow(2.0, 3.0) > 7.99 && pow(2.0, 3.0) < 8.01);
}

TEST(pow_zero_exp) {
    ASSERT(pow(5.0, 0.0) > 0.99 && pow(5.0, 0.0) < 1.01);
}

TEST(pow_one_exp) {
    ASSERT(pow(7.0, 1.0) > 6.99 && pow(7.0, 1.0) < 7.01);
}

// === main ===

int main(void)
{
    printf("=== lib.c tests ===\n");

    RUN_TEST(strlen_empty);
    RUN_TEST(strlen_hello);
    RUN_TEST(strlen_single_char);
    RUN_TEST(strlen_with_spaces);

    RUN_TEST(strcmp_equal);
    RUN_TEST(strcmp_less);
    RUN_TEST(strcmp_greater);
    RUN_TEST(strcmp_empty_strings);
    RUN_TEST(strcmp_first_empty);
    RUN_TEST(strcmp_second_empty);
    RUN_TEST(strcmp_prefix);

    RUN_TEST(strncmp_equal_within_n);
    RUN_TEST(strncmp_diff_within_n);
    RUN_TEST(strncmp_zero_n);

    RUN_TEST(strcpy_basic);
    RUN_TEST(strcpy_empty);

    RUN_TEST(strncpy_basic);
    RUN_TEST(strncpy_truncate);

    RUN_TEST(strchr_found);
    RUN_TEST(strchr_not_found);
    RUN_TEST(strchr_first_char);

    RUN_TEST(strrchr_found_last);
    RUN_TEST(strrchr_not_found);

    RUN_TEST(pow_basic);
    RUN_TEST(pow_zero_exp);
    RUN_TEST(pow_one_exp);

    TEST_REPORT();
}
```

### test_memb.c — メモリブロックテスト

```c
#include "test_framework.h"
#include "mock_kernel.h"

// memb.c は MEMB_CONCAT マクロで静的配列を定義する仕組み
// テストではダミーの配列を用意

// memb.c のインクルードに必要な定義を確認
// MEMB(name, structure_size, num) マクロでブロックプールを宣言

TEST(memb_alloc_single) {
    // ブロックプール初期化
    // 1ブロック割り当て
    // NULLでないことを確認
}

TEST(memb_alloc_all) {
    // 全ブロック割り当て
    // 次の割り当てがNULLを返すことを確認
}

TEST(memb_free_and_realloc) {
    // 全割り当て → 1つ解放 → 再割り当て成功
}

TEST(memb_free_invalid) {
    // プール外のアドレスを解放 → エラー
}

int main(void)
{
    printf("=== memb.c tests ===\n");

    RUN_TEST(memb_alloc_single);
    RUN_TEST(memb_alloc_all);
    RUN_TEST(memb_free_and_realloc);
    RUN_TEST(memb_free_invalid);

    TEST_REPORT();
}
```

## コンパイル上の注意

### lib.c の関数名衝突

`strlen`, `strcmp` 等はホストのlibc にも存在する。
名前衝突を避けるため:

- **方法A**: `-ffreestanding -nostdlib` でホストのlibcを外す（テストのprintf等が使えなくなる）
- **方法B**: テストフレームワークだけlibcを使い、テスト対象はリンク順で優先させる
- **方法C**: テスト内で `#define strlen sodex_strlen` のようにリネーム

**推奨: 方法B**。テスト対象のオブジェクトファイルを先にリンクし、
リンカの「先勝ち」ルールで カーネル版の関数を使わせる。

もしくは、lib.c の関数名にプレフィックスを付けるリファクタリング（Plan 03）の後なら
衝突は起きない。

### pow/log の衝突

`pow()` と `log()` はlibm と衝突する。
テスト時は `-lm` をリンクしないか、リネームで対応。

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `tests/test_lib.c` | 新規。文字列・数学関数テスト |
| `tests/test_memb.c` | 新規。メモリブロック管理テスト |

## 依存関係

- Plan 01（テストフレームワーク）が完了していること

## 完了条件

- [ ] strlen, strcmp, strncmp, strcpy, strncpy, strchr, strrchr のテストが全てPASS
- [ ] pow のテストがPASS（精度は浮動小数点誤差を許容）
- [ ] memb_init, memb_alloc, memb_free のテストが全てPASS
- [ ] `make test` で全テスト実行可能
