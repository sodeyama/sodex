# Plan 1.3: NE2000 割り込みハンドラの実装

## 概要

NE2000の割り込みハンドラ `i2Bh_ne2000_interrupt()` を、ステータス表示のみから
実際のパケット受信処理を起動するよう改修する。

## 現状

### 割り込みハンドラ（src/drivers/ne2000.c:66-81）
```c
PUBLIC void i2Bh_ne2000_interrupt()
{
    _kprintf("ne2000 int\n");
    u_int8_t status = in8(io_base+I_ISR);
    if (status & ISR_PRX) {
        _kprintf("ISR_PRX\n");
    } else if (status & ISR_PTX) {
        _kprintf("ISR_PTX\n");
    } else if (status & ISR_OVW) {
        _kprintf("ISR_OVW\n");
    } else {
        _kprintf("other\n");
    }
    pic_eoi();
}
```

### 問題点

1. ステータス表示のみで実処理なし
2. `pic_eoi()` が引数なしで呼ばれている。正しくは `pic_eoi(11)`（IRQ11）
3. ISRの割り込みビットがクリアされていない → 再割り込みが発生しない
4. `else if` チェーンなので、複数フラグが同時に立った場合に最初の1つしか処理されない

### アセンブリスタブ（src/ihandlers.S:336-346）
```asm
.global asm_i2Bh
.align 4, 0x90
asm_i2Bh:
    pusha
    push    %es
    push    %ds
    call    i2Bh_ne2000_interrupt
    pop     %ds
    pop     %es
    popa
    iret
```

### IDT登録（src/idt.c:99）
```c
set_trap_gate(NE2K_QEMU_IRQ, &asm_i2Bh);
// NE2K_QEMU_IRQ = 0x2B = 43
// IRQ番号: 43 - 32 = 11（スレーブPIC）
```

### pic_eoi()（src/idt.c:438-448）
```c
PUBLIC void pic_eoi(int irq)
{
    if (irq < 8) {
        out8(0x20, 0x60 + irq);
    } else if (irq < 16) {
        disableInterrupt();
        out8(0xA0, 0x60 + irq - 8);
        out8(0x20, 0x62);
        enableInterrupt();
    }
}
```

## 設計方針

- 割り込みハンドラ内では**最小限の処理**のみ行う（長時間処理を避ける）
- パケット受信フラグ（`ne2000_rx_pending`）をセットし、メインループで処理
- ISRの全ビットをチェックし、対応する処理を行う
- 割り込みビットを確実にクリアする

## 実装

### グローバル変数の追加

**ファイル**: `src/drivers/ne2000.c`

```c
PUBLIC volatile u_int8_t ne2000_rx_pending = 0;
PRIVATE volatile u_int8_t ne2000_tx_complete = 0;
```

### 割り込みハンドラの改修

**ファイル**: `src/drivers/ne2000.c`

```c
#define NE2000_IRQ 11  // 0x2B - 0x20 = 0x0B = 11

PUBLIC void i2Bh_ne2000_interrupt()
{
    u_int8_t status = in8(io_base + I_ISR);

    // 受信完了
    if (status & ISR_PRX) {
        ne2000_rx_pending = 1;
    }

    // 受信エラー
    if (status & ISR_RXE) {
        // エラーカウンタをインクリメント（デバッグ用）
        // 特別な処理は不要。次の受信で回復する
    }

    // 送信完了
    if (status & ISR_PTX) {
        ne2000_tx_complete = 1;
    }

    // 送信エラー
    if (status & ISR_TXE) {
        // エラーカウンタをインクリメント（デバッグ用）
    }

    // 受信バッファオーバーフロー
    if (status & ISR_OVW) {
        // NE2000仕様に従ったオーバーフロー回復手順:
        // 1. CRでSTOPコマンド発行
        out8(io_base + O_CR, CR_STP | CR_RD2);
        // 2. RBCR0/1をクリア
        out8(io_base + O_RBCR0, 0);
        out8(io_base + O_RBCR1, 0);
        // 3. 少し待つ
        // 4. TCRをループバックモードに設定
        out8(io_base + O_TCR, TCR_LB_NIC);
        // 5. CRでSTARTコマンド発行
        out8(io_base + O_CR, CR_STA | CR_RD2);
        // 6. TCRを通常モードに戻す
        out8(io_base + O_TCR, TCR_LB_NORMAL);
        // オーバーフロー後は受信もチェック
        ne2000_rx_pending = 1;
    }

    // 処理済みフラグをクリア（ISRに書き戻す）
    out8(io_base + O_ISR, status);

    // EOI送信（IRQ11: スレーブPIC経由）
    pic_eoi(NE2000_IRQ);
}
```

### ne2000.hへの公開宣言追加

**ファイル**: `src/include/ne2000.h`

```c
// 既存の宣言に追加
EXTERN volatile u_int8_t ne2000_rx_pending;
```

## ISRフラグ一覧と処理

| フラグ | 値 | 意味 | 処理 |
|--------|-----|------|------|
| ISR_PRX | 0x01 | パケット受信完了 | rx_pendingフラグセット |
| ISR_PTX | 0x02 | パケット送信完了 | tx_completeフラグセット |
| ISR_RXE | 0x04 | 受信エラー | ログ（任意） |
| ISR_TXE | 0x08 | 送信エラー | ログ（任意） |
| ISR_OVW | 0x10 | オーバーフロー | NICリセットシーケンス |
| ISR_RDC | 0x40 | リモートDMA完了 | DMA待ちループ側で処理 |

## テスト

### 割り込み発生の確認

```c
// 一時的なデバッグ出力（動作確認後に除去）
PUBLIC void i2Bh_ne2000_interrupt()
{
    u_int8_t status = in8(io_base + I_ISR);
    _kprintf("NE2000 IRQ: ISR=0x%x\n", status);
    // ... 以下の処理 ...
}
```

### テストシナリオ

1. QEMUで起動し、NE2000初期化後に割り込みが発生するか確認
2. ホストからpingを送り、ISR_PRXフラグが立つか確認
3. `ne2000_rx_pending` が1にセットされるか確認
4. ISRクリア後に再度割り込みが発生するか確認

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `src/drivers/ne2000.c` | 割り込みハンドラ改修、グローバル変数追加 |
| `src/include/ne2000.h` | `ne2000_rx_pending` のextern宣言追加 |

## 依存関係

- Plan 1.2（ne2000_receive）が完了していなくても、割り込みハンドラ単体で実装可能
- `ne2000_rx_pending` はPlan 1.5（ネットワークポーリングループ）で参照される

## 完了条件

- [ ] 割り込みハンドラがISRの全ビットを正しくチェックする
- [ ] `pic_eoi(11)` が正しく呼ばれる（引数付き）
- [ ] ISRビットが書き戻しでクリアされる
- [ ] `ne2000_rx_pending` がパケット受信時にセットされる
- [ ] オーバーフロー時のリカバリ手順が実装されている
- [ ] デバッグ出力で割り込み発生を確認できる
