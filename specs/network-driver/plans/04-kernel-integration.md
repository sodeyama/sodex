# Plan 1.4: カーネルへの統合

## 概要

NE2000ドライバとuIPスタックをカーネルの初期化シーケンスに組み込み、
起動時にネットワークが使える状態にする。

## 現状

### kernel.c の NE2000 関連（src/kernel.c:86-87）
```c
//init_ne2000();
//_kputs(" KERNEL: SETUP NE2000\n");
```
完全にコメントアウトされている。

### kernel.c の初期化シーケンス（現在の順序）
```
start_kernel()
  ├── init_screen()
  ├── init_gdt()
  ├── init_setupidthandlers()   ← ここでset_trap_gate(0x2B, &asm_i2Bh)が登録済み
  ├── init_pit()
  ├── init_memory()
  ├── init_keyboard()
  ├── init_dma()
  ├── init_uhci() / init_fdc()
  ├── init_page()
  ├── init_pci()
  ├── init_ext3fs()
  ├── // init_ne2000()          ← コメントアウト
  ├── setup_syscall()
  ├── init_process()
  └── init_signal()
```

### IDTでのNE2000割り込み登録（src/idt.c:99）
```c
set_trap_gate(NE2K_QEMU_IRQ, &asm_i2Bh);
```
IDTへの登録は `init_setupidthandlers()` 内で**既に行われている**。

## 実装手順

### Step 1: init_ne2000() のコメントアウト解除

**ファイル**: `src/kernel.c`

```c
// 変更前
//init_ne2000();
//_kputs(" KERNEL: SETUP NE2000\n");

// 変更後
init_ne2000();
_kputs(" KERNEL: SETUP NE2000\n");
```

配置場所: `init_pci()` の後。PCIバスの初期化後にNICを初期化する。

### Step 2: uIP初期化の追加

**ファイル**: `src/kernel.c`

`init_ne2000()` の直後にuIPスタックの初期化を追加:

```c
init_ne2000();
_kputs(" KERNEL: SETUP NE2000\n");

// uIP TCP/IPスタック初期化
uip_init();

// IPアドレス設定（QEMU user net用）
uip_ipaddr_t ipaddr;

// ホストIPアドレス: 10.0.2.15
uip_ipaddr(&ipaddr, 10, 0, 2, 15);
uip_sethostaddr(&ipaddr);

// サブネットマスク: 255.255.255.0
uip_ipaddr(&ipaddr, 255, 255, 255, 0);
uip_setnetmask(&ipaddr);

// デフォルトゲートウェイ: 10.0.2.2
uip_ipaddr(&ipaddr, 10, 0, 2, 2);
uip_setdraddr(&ipaddr);

// MACアドレスをuIPに設定
// ※ init_ne2000()内でNICに設定するMACアドレスと一致させること
struct uip_eth_addr mac = {{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
uip_setethaddr(mac);

_kputs(" KERNEL: SETUP uIP\n");
```

### Step 3: NE2000のMACアドレス修正

**ファイル**: `src/drivers/ne2000.c`

現在ハードコードされているMAC `1.1.1.1.1.1` を、QEMUが認識する正しいMACに変更:

```c
// 変更前（init_ne2000内）
// MAC: 01:01:01:01:01:01

// 変更後
// QEMU互換のMAC: 52:54:00:12:34:56
out8(io_base + IO_PAR0, 0x52);
out8(io_base + IO_PAR1, 0x54);
out8(io_base + IO_PAR2, 0x00);
out8(io_base + IO_PAR3, 0x12);
out8(io_base + IO_PAR4, 0x34);
out8(io_base + IO_PAR5, 0x56);
```

注意: QEMUはNICに自動でMACアドレスを割り当てる場合がある。
`-net nic,macaddr=52:54:00:12:34:56` で明示指定も可能。

### Step 4: 必要なヘッダのインクルード

**ファイル**: `src/kernel.c`

```c
#include <uip.h>
#include <uip_arp.h>
#include <ne2000.h>
```

### Step 5: PIMのIMR（割り込みマスクレジスタ）確認

NE2000はIRQ11（スレーブPIC、IRQ3）を使用する。
PICの初期化でIRQ11がマスクされていないか確認が必要。

**確認場所**: `src/idt.c` 内のPIC初期化部分

スレーブPICのIMR（ポート0xA1）でbit3（IRQ11）が0（有効）であること:
```c
// IRQ11を有効にする（bit3をクリア）
u_int8_t mask = in8(0xA1);
mask &= ~(1 << 3);  // IRQ11 = スレーブのIRQ3
out8(0xA1, mask);
```

## QEMU ネットワーク設定

### 起動コマンド

```bash
qemu-system-i386 \
    -fda src/bin/fsboot.bin \
    -net nic,model=ne2k_isa,irq=11,iobase=0xc100 \
    -net user
```

### QEMU user netのアドレス体系

| 項目 | アドレス |
|------|---------|
| ゲートウェイ/ホスト | 10.0.2.2 |
| DNSサーバー | 10.0.2.3 |
| ゲストIP | 10.0.2.15（デフォルト） |
| ネットマスク | 255.255.255.0 |

## テスト

### 起動確認

1. QEMUで起動
2. `KERNEL: SETUP NE2000` と `KERNEL: SETUP uIP` が表示されることを確認
3. カーネルパニックやハングが発生しないことを確認

### 割り込み確認

NE2000初期化後、QEMUのuser netがARP要求を送信してくるはず。
割り込みハンドラ（Plan 1.3）のデバッグ出力で確認:
```
NE2000 IRQ: ISR=0x01  ← ISR_PRX（パケット受信）
```

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `src/kernel.c` | init_ne2000()解除、uIP初期化追加、ヘッダインクルード |
| `src/drivers/ne2000.c` | MACアドレスをQEMU互換に変更 |
| `src/idt.c` | IRQ11のマスク解除（必要な場合） |

## 依存関係

- Plan 1.1（clock_time）: uIPタイマーが動くために必要だが、初期化自体には不要
- Plan 1.3（割り込みハンドラ）: 割り込みの実処理に必要

## 完了条件

- [ ] `init_ne2000()` がコメントアウトなしで呼ばれる
- [ ] `uip_init()` が呼ばれ、IPアドレスが設定される
- [ ] MACアドレスがNICとuIPで一致している
- [ ] 起動時にパニックやハングが発生しない
- [ ] NE2000の割り込みがPICでマスクされていない
- [ ] QEMUからの最初のパケット（ARP等）で割り込みが発生する
