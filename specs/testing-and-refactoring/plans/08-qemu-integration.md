# Plan 08: QEMU統合テストフレームワーク

## 概要

ハードウェア依存の機能（割り込み、ページング、プロセス管理等）はホスト側でテストできない。
QEMU上でカーネル内テストを自動実行し、結果をシリアルポート経由で取得する仕組みを構築する。

## 設計

### テスト実行フロー

```
1. make test-qemu（ホスト側）
2. テストカーネルをビルド（通常カーネル + テストコード）
3. QEMUを自動起動（-serial stdio -nographic）
4. カーネルがテストを自動実行
5. 結果をシリアルポートに出力
6. QEMUが自動終了（テスト完了後にout8で電源OFF）
7. ホスト側で出力を解析してPASS/FAIL判定
```

### カーネル内テストランナー

```c
// src/test/ktest.c（TEST_BUILDフラグ有効時のみビルド）

#include <vga.h>
#include <io.h>

// シリアルポート出力（COM1: 0x3F8）
PRIVATE void serial_putc(char c)
{
    while (!(in8(0x3F8 + 5) & 0x20));  // 送信可能待ち
    out8(0x3F8, c);
}

PRIVATE void serial_puts(const char *s)
{
    while (*s) serial_putc(*s++);
}

// QEMU終了（ISA debug exit device）
PRIVATE void qemu_exit(int code)
{
    out8(0xF4, code);  // -device isa-debug-exit で有効
}

// テスト実行
PUBLIC void run_kernel_tests(void)
{
    serial_puts("=== Kernel Integration Tests ===\n");

    // テスト関数をここで呼ぶ
    test_memory_integration();
    test_pit_tick();
    test_keyboard_scancode();
    // ...

    serial_puts("=== Tests Complete ===\n");
    qemu_exit(0);
}
```

### QEMUの起動オプション

```bash
qemu-system-i386 \
    -fda src/bin/fsboot.bin \
    -nographic \
    -serial stdio \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -no-reboot \
    | tee test_output.log
```

- `-nographic`: VGA画面を無効化
- `-serial stdio`: シリアル出力を標準出力に
- `-device isa-debug-exit`: テスト完了後にQEMUを終了するためのデバイス
- `-no-reboot`: トリプルフォルトでリブートせず終了

### テストカーネルのビルド

```makefile
# 通常のstart_kernel()の代わりにテストランナーを呼ぶビルド
test-qemu: CFLAGS += -DTEST_BUILD
test-qemu: build
	qemu-system-i386 ... | tee test_output.log
	@grep -q "Tests Complete" test_output.log && echo "ALL PASS" || echo "FAILED"
```

### kernel.c での分岐

```c
PUBLIC void start_kernel(void)
{
    init_screen();
    init_setupgdt();
    init_setupidthandlers();
    init_setupidt();
    init_pit();
    init_mem();

#ifdef TEST_BUILD
    run_kernel_tests();
    // ここでQEMU終了（run_kernel_tests内でqemu_exit呼び出し）
#else
    // 通常の初期化続行
    init_key();
    // ...
#endif
}
```

## テストケース例

### PIT動作確認

```c
void test_pit_tick(void)
{
    extern volatile u_int32_t kernel_tick;
    u_int32_t start = kernel_tick;
    // 短いビジーウェイト
    for (volatile int i = 0; i < 1000000; i++);
    u_int32_t end = kernel_tick;

    if (end > start) {
        serial_puts("[PASS] pit_tick: counter incrementing\n");
    } else {
        serial_puts("[FAIL] pit_tick: counter not incrementing\n");
    }
}
```

### メモリ管理統合テスト

```c
void test_memory_integration(void)
{
    void *p1 = kalloc(1024);
    void *p2 = kalloc(2048);

    if (p1 && p2 && p1 != p2) {
        serial_puts("[PASS] memory: kalloc returns valid pointers\n");
    } else {
        serial_puts("[FAIL] memory: kalloc failed\n");
    }

    kfree(p1);
    kfree(p2);

    void *p3 = kalloc(1024);
    if (p3) {
        serial_puts("[PASS] memory: realloc after free\n");
    } else {
        serial_puts("[FAIL] memory: realloc after free failed\n");
    }
    kfree(p3);
}
```

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `src/test/ktest.c` | 新規。カーネル内テストランナー |
| `src/test/ktest.h` | 新規。テスト関数宣言 |
| `src/kernel.c` | `#ifdef TEST_BUILD` で分岐追加 |
| `src/makefile` | test-qemu ターゲット追加 |

## 依存関係

- Plan 01（テストフレームワーク）のホスト側テストが安定した後に着手
- PIT、メモリ管理等が動作していることが前提

## 完了条件

- [ ] `make test-qemu` でQEMUが自動起動・テスト実行・自動終了する
- [ ] テスト結果がシリアル出力で確認できる
- [ ] 少なくともメモリ管理とPITのテストがPASS
