# Plan 01: ホスト側テストフレームワーク構築

## 概要

macOS上でSodexのハードウェア非依存コードをコンパイル・テストするための
軽量テストフレームワークを構築する。

## 設計方針

- 外部テストライブラリは使わない（Sodexの精神に合わせてミニマル）
- テスト用のassertマクロと結果レポートだけの最小限フレームワーク
- ホストのgcc（x86_64）でコンパイル。カーネルのクロスコンパイラは使わない
- テスト対象のソースファイルをそのままインクルード or リンク

## ディレクトリ構成

```
tests/
├── Makefile              — テストのビルド・実行
├── test_framework.h      — assertマクロ、テストランナー
├── test_lib.c            — src/lib/ のテスト
├── test_memory.c         — memory.c のテスト
├── test_memb.c           — memb.c のテスト
├── test_ext3fs_parse.c   — ext3fsパース部分のテスト
└── mocks/
    ├── mock_io.h         — in8/out8等のスタブ
    ├── mock_vga.h        — _kprintf等のスタブ
    └── mock_kernel.h     — カーネルグローバル変数のスタブ
```

## テストフレームワーク

### test_framework.h

```c
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

static int _test_count = 0;
static int _test_pass = 0;
static int _test_fail = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        _test_count++; \
        printf("  [TEST] %s ... ", #name); \
        test_##name(); \
        _test_pass++; \
        printf("PASS\n"); \
    } \
    static void test_##name(void)

#define ASSERT(expr) \
    do { \
        if (!(expr)) { \
            printf("FAIL\n"); \
            printf("    %s:%d: assertion failed: %s\n", __FILE__, __LINE__, #expr); \
            _test_fail++; \
            _test_pass--; \
            return; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            printf("FAIL\n"); \
            printf("    %s:%d: expected %d, got %d\n", __FILE__, __LINE__, (int)(b), (int)(a)); \
            _test_fail++; \
            _test_pass--; \
            return; \
        } \
    } while (0)

#define ASSERT_STR_EQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            printf("FAIL\n"); \
            printf("    %s:%d: expected \"%s\", got \"%s\"\n", __FILE__, __LINE__, (b), (a)); \
            _test_fail++; \
            _test_pass--; \
            return; \
        } \
    } while (0)

#define ASSERT_NULL(ptr) ASSERT((ptr) == NULL)
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL)

#define RUN_TEST(name) run_test_##name()

#define TEST_REPORT() \
    do { \
        printf("\n--- Results: %d/%d passed", _test_pass, _test_count); \
        if (_test_fail > 0) printf(", %d FAILED", _test_fail); \
        printf(" ---\n"); \
        return _test_fail > 0 ? 1 : 0; \
    } while (0)

#endif
```

## モック層

### mock_io.h — I/Oポート操作のスタブ

```c
#ifndef MOCK_IO_H
#define MOCK_IO_H

#include <stdint.h>

// 型定義の互換（カーネルの型とホストの型を橋渡し）
typedef uint8_t  u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef int32_t  int32_t;

// I/O関数のスタブ（何もしない）
static inline void out8(uint16_t port, uint8_t val) { (void)port; (void)val; }
static inline void out16(uint16_t port, uint16_t val) { (void)port; (void)val; }
static inline uint8_t in8(uint16_t port) { (void)port; return 0; }
static inline uint16_t in16(uint16_t port) { (void)port; return 0; }
static inline void enableInterrupt(void) {}
static inline void disableInterrupt(void) {}

#endif
```

### mock_vga.h — 画面出力のスタブ

```c
#ifndef MOCK_VGA_H
#define MOCK_VGA_H

#include <stdio.h>
#include <stdarg.h>

// _kprintf をホストのprintfにリダイレクト
#define _kprintf printf
#define _kputs(s) fputs(s, stdout)
#define _kputc(c) putchar(c)

#endif
```

### mock_kernel.h — カーネル定数・マクロのスタブ

```c
#ifndef MOCK_KERNEL_H
#define MOCK_KERNEL_H

// sodex/const.h の代替
#define PUBLIC
#define PRIVATE static
#define EXTERN extern
#define TRUE  1
#define FALSE 0
#define BLOCK_SIZE 4096

// __PAGE_OFFSETのスタブ（テスト時は0）
#define __PAGE_OFFSET 0

#endif
```

## Makefile

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -I. -Imocks -I../src/include

# テスト対象のソースディレクトリ
SRC = ../src

# テスト実行ファイル
TESTS = test_lib test_memb test_memory

.PHONY: all clean test

all: $(TESTS)

test: $(TESTS)
	@echo "=== Running all tests ==="
	@for t in $(TESTS); do echo "\n--- $$t ---"; ./$$t; done

test_lib: test_lib.c $(SRC)/lib/lib.c
	$(CC) $(CFLAGS) -DTEST_BUILD -o $@ $^

test_memb: test_memb.c $(SRC)/lib/memb.c
	$(CC) $(CFLAGS) -DTEST_BUILD -o $@ $^

test_memory: test_memory.c
	$(CC) $(CFLAGS) -DTEST_BUILD -o $@ $^

clean:
	rm -f $(TESTS)
```

## コンパイル互換性の課題

### 型定義の衝突

カーネルコードは `u_int8_t` 等を `<sys/types.h>` や独自ヘッダで定義。
ホストのgccにも同名の型がある場合、衝突する可能性。

対策:
- `mock_io.h` でホスト側の型定義を提供
- `-DTEST_BUILD` フラグでカーネル固有のインクルードを条件分岐

```c
// カーネルソース内
#ifdef TEST_BUILD
#include "mock_kernel.h"
#include "mock_io.h"
#else
#include <sodex/const.h>
#include <io.h>
#endif
```

**ただし**: 既存ソースの修正は最小限にしたい。
可能であればテスト側のインクルードパスとマクロ定義だけで解決する。

### インクルードパスの調整

カーネルのソースは `#include <xxx.h>` で `src/include/` を参照する。
テスト側で `-I../src/include` を指定することでパスを解決。

ただし、`<io.h>` のようなハードウェア依存ヘッダをインクルードするファイルは
モックヘッダで置換する必要がある。

## テスト実行フロー

```bash
cd tests/
make test
```

期待出力:
```
=== Running all tests ===

--- test_lib ---
  [TEST] strlen_empty ... PASS
  [TEST] strlen_hello ... PASS
  [TEST] strcmp_equal ... PASS
  ...
--- Results: 15/15 passed ---

--- test_memb ---
  [TEST] memb_init ... PASS
  ...
```

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `tests/` | 新規ディレクトリ作成 |
| `tests/Makefile` | テストビルドシステム |
| `tests/test_framework.h` | assertマクロ、テストランナー |
| `tests/mocks/*.h` | I/O、VGA、カーネル定数のモック |

## 完了条件

- [ ] `make test` でテストが全てコンパイル・実行できる
- [ ] test_framework.h のASSERT系マクロが正しく動作する
- [ ] PASSとFAILが正しくカウント・表示される
- [ ] モック層でカーネルのハードウェア依存を分離できる
- [ ] 少なくとも1つのテスト（test_lib）が通る
