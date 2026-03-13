# Plan 1.1: clock_time() の実装

## 概要

uIPのTCP再送タイムアウト、ARPキャッシュタイムアウト等が機能するために、
`clock_time()` がカーネルの経過時間を返すようにする。

## 現状

### clock_time()（src/net/clock-arch.c）
```c
clock_time_t clock_time(void)
{
    // gettimeofday()がコメントアウトされ、暗黙的に0を返す
}
```

### clock-arch.h（src/include/clock-arch.h）
```c
typedef int clock_time_t;
#define CLOCK_CONF_SECOND 1000  // 1秒 = 1000（ミリ秒単位を想定）
```

### PIT（src/pit8254.c）
PIT初期化が**コメントアウト**されている:
```c
PUBLIC void init_pit()
{
    //pit_setcounter(LATCH);
}
```

PITのカウンタ設定は存在するがtickカウンタ変数は未定義。

### PIT定数（src/include/pit8254.h）
```
PIT_COUNTER0 = 0x040
PIT_CONTROL  = 0x043
PITCTL_COUNTER0 | PITCTL_RW_16 | PITCTL_MODE2 | PITCTL_CNTMODE_BIN
```

## 実装手順

### Step 1: PITの有効化とtickカウンタの追加

**ファイル**: `src/pit8254.c`

```c
#include <sodex/const.h>
#include <pit8254.h>

// 10msごとに割り込み（100Hz）
// PIT入力クロック: 1193182Hz
// LATCH = 1193182 / 100 = 11932
#define PIT_HZ    100
#define PIT_LATCH (1193182 / PIT_HZ)

PUBLIC volatile u_int32_t kernel_tick = 0;

PUBLIC void init_pit()
{
    pit_setcounter(PIT_LATCH);
}
```

確認事項:
- [ ] `LATCH` マクロが既に定義されているか確認。されていればその値を使う
- [ ] `pit_setcounter()` の実装を確認し、正しいPITレジスタに書き込んでいるか検証

### Step 2: PIT割り込みハンドラでtickをインクリメント

PITの割り込みハンドラ（IRQ0、IDT 0x20）がどこにあるか確認し、
`kernel_tick++` を追加する。

**想定場所**: `src/idt.c` または `src/timer.c` 内のタイマー割り込みハンドラ

```c
// PIT割り込みハンドラ（IRQ0）内に追加
extern volatile u_int32_t kernel_tick;
kernel_tick++;
```

確認事項:
- [ ] 既存のPIT割り込みハンドラの場所を特定
- [ ] ハンドラ内で `pic_eoi(0)` が呼ばれているか確認

### Step 3: clock_time() の実装

**ファイル**: `src/net/clock-arch.c`

```c
#include "clock-arch.h"

extern volatile u_int32_t kernel_tick;

clock_time_t clock_time(void)
{
    // kernel_tickは10ms単位（100Hz）
    // CLOCK_CONF_SECOND = 1000 なので、tickを10倍してミリ秒に変換
    return kernel_tick * (1000 / PIT_HZ);
}
```

あるいは、`CLOCK_CONF_SECOND` を100に変更してtickをそのまま返す方が簡潔:

```c
// clock-arch.h を変更
#define CLOCK_CONF_SECOND 100  // 1秒 = 100 tick

// clock-arch.c
clock_time_t clock_time(void)
{
    return kernel_tick;
}
```

**推奨**: 後者（CLOCK_CONF_SECOND = 100）の方がオーバーフローが遅く、乗算も不要。

### Step 4: init_pit() のコメントアウト解除

**ファイル**: `src/kernel.c`

`start_kernel()` 内の PIT初期化が正しく呼ばれているか確認。
コメントアウトされていれば解除する。

## テスト

```c
// kernel.c の start_kernel() 末尾等で動作確認
_kprintf("tick test start\n");
u_int32_t start = clock_time();
// 適当な待ちループ
for (volatile int i = 0; i < 10000000; i++);
u_int32_t end = clock_time();
_kprintf("elapsed: %d ticks\n", end - start);
```

tickが0以外の値を返し、時間経過で増加していれば成功。

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `src/pit8254.c` | `kernel_tick` 変数追加、`init_pit()` 有効化 |
| `src/net/clock-arch.c` | `clock_time()` を `kernel_tick` ベースに実装 |
| `src/include/clock-arch.h` | `CLOCK_CONF_SECOND` を100に変更（任意） |
| PIT割り込みハンドラ | `kernel_tick++` を追加 |
| `src/kernel.c` | `init_pit()` がコメントアウトされていれば解除 |

## 完了条件

- [ ] `kernel_tick` が割り込みごとにインクリメントされる
- [ ] `clock_time()` が0以外の値を返す
- [ ] 時間経過で `clock_time()` の戻り値が単調増加する
- [ ] uIPの `timer_expired()` が正常に動作する
