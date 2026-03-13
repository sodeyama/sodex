# Plan 1.2: NE2000 受信処理の実装

## 概要

NE2000ドライバの `ne2000_receive()` を実装し、NICのリングバッファからパケットを
`uip_buf` に読み出せるようにする。

## 現状

### ne2000_receive()（src/drivers/ne2000.c:123-125）
```c
PUBLIC int ne2000_receive()
{
}
```
完全に空。

### NE2000内部メモリレイアウト
```
アドレス（ページ単位、1ページ=256バイト）:
  0x40-0x45: 送信バッファ（6ページ = 1,536バイト）
  0x46-0x80: 受信リングバッファ（58ページ = 14,848バイト）

定数（ne2000.h）:
  SEND_ADDR   = 0x40
  PSTART_ADDR = 0x46  （受信リング開始）
  PSTOP_ADDR  = 0x80  （受信リング終了）
  BNRY_ADDR   = 0x46  （初期バウンダリ）
  CURR_ADDR   = 0x46  （初期カレントページ）
```

### 送信側のDMAシーケンス（参考: write_remote_dma）

受信の逆操作として参考にする。`src/drivers/ne2000.c:221-259`:

```c
PRIVATE void write_remote_dma(u_int16_t addr, void* buf, u_int16_t len)
{
    u_int16_t end = (len%2) == 1 ? ((len+1)>>1) : (len>>1);
    u_int16_t *p = (u_int16_t*)buf;

    // DMAアドレス設定（注意: RSAR0とRSAR1の使い方が通常と逆）
    out8(io_base+O_RSAR1, addr & 0xff);     // アドレス低バイト → RSAR1
    out8(io_base+O_RSAR0, (addr >> 8));      // アドレス高バイト → RSAR0

    // バイト数設定
    out8(io_base+O_RBCR0, len & 0xff);
    out8(io_base+O_RBCR1, (len >> 8));

    // 書き込みモード開始
    out8(io_base+O_CR, CR_STA | CR_RD_WRITE);

    // データを16ビット単位で書き込み
    for (int i = 0; i < end; i++) {
        out16(io_base + DATA_PORT_OFFSET, *p);
        p++;
    }

    // DMA完了待ち
    while (!(in8(io_base+I_ISR) & ISR_RDC) && count < WAIT_MAX_COUNT);
}
```

**重要な注意**: このコードではRSAR0/RSAR1の使い方が標準NE2000仕様と逆になっている。
受信側でも同じパターンに従うこと。

## NE2000受信リングバッファの仕組み

```
                  PSTART (0x46)                      PSTOP (0x80)
                      |                                   |
                      v                                   v
  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+
  |   | T | T | T | T | R | R | R | R | R | R | R | R | R |
  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+
  0x40              0x45 0x46                          0x80

  T = 送信バッファ
  R = 受信リングバッファ

  BNRY: 読み出し済みの最後のページ（ソフトウェアが管理）
  CURR: NICが次に書き込むページ（ハードウェアが管理、Page1レジスタ）

  BNRY == CURR → バッファ空
  BNRY != CURR → 未読パケットあり
```

### 受信パケットヘッダ（各パケットの先頭4バイト）

```
Offset  Size  Description
  0     1     recv_status: 受信ステータス（ISR_PRX相当のフラグ）
  1     1     next_page:   次のパケットの開始ページ番号
  2     2     length:      パケット長（ヘッダ4バイト含む）
```

## 実装

### read_remote_dma() の追加

`write_remote_dma()` の逆操作。NIC RAMからホストメモリへ読み出す。

**ファイル**: `src/drivers/ne2000.c`

```c
PRIVATE int read_remote_dma(u_int16_t addr, void *buf, u_int16_t len)
{
    u_int16_t word_count = (len % 2) == 1 ? ((len + 1) >> 1) : (len >> 1);
    u_int16_t *p = (u_int16_t *)buf;

    // ISR_RDCビットをクリア
    out8(io_base + O_ISR, ISR_RDC);

    // DMAアドレス設定（write_remote_dmaと同じパターン）
    out8(io_base + O_RSAR1, addr & 0xff);
    out8(io_base + O_RSAR0, (addr >> 8));

    // バイト数設定
    out8(io_base + O_RBCR0, len & 0xff);
    out8(io_base + O_RBCR1, (len >> 8));

    // 読み出しモード開始
    out8(io_base + O_CR, CR_STA | CR_RD_READ);

    // データを16ビット単位で読み出し
    for (u_int16_t i = 0; i < word_count; i++) {
        *p = in16(io_base + DATA_PORT_OFFSET);
        p++;
    }

    // DMA完了待ち
    int count = 0;
    while (TRUE) {
        u_int8_t status = in8(io_base + I_ISR);
        if ((status & ISR_RDC) || count > WAIT_MAX_COUNT)
            break;
        count++;
    }

    return 0;
}
```

### ne2000_receive() の実装

**ファイル**: `src/drivers/ne2000.c`

```c
PUBLIC int ne2000_receive()
{
    u_int8_t curr, bnry;

    // Page1に切り替えてCURRを読む
    out8(io_base + I_CR, CR_PAGE1 | CR_STA | CR_RD2);
    curr = in8(io_base + IO_CURR);
    // Page0に戻す
    out8(io_base + I_CR, CR_PAGE0 | CR_STA | CR_RD2);

    // BNRYを読む
    bnry = in8(io_base + I_BNRY);

    // バッファ空チェック
    if (bnry == curr) {
        return 0;
    }

    // パケットヘッダ読み出し（4バイト）
    u_int8_t pkt_hdr[4];
    u_int16_t pkt_addr = bnry << 8;  // ページ番号 → バイトアドレス
    read_remote_dma(pkt_addr, pkt_hdr, 4);

    u_int8_t  recv_status = pkt_hdr[0];
    u_int8_t  next_page   = pkt_hdr[1];
    u_int16_t pkt_len     = pkt_hdr[2] | (pkt_hdr[3] << 8);

    // パケット長からヘッダ4バイトを引いた実データ長
    u_int16_t data_len = pkt_len - 4;

    // 異常値チェック
    if (data_len > UIP_BUFSIZE || data_len == 0) {
        // BNRYを進めてスキップ
        out8(io_base + O_BNRY, next_page == PSTART_ADDR ? PSTOP_ADDR - 1 : next_page - 1);
        return -1;
    }

    // パケットデータ読み出し（ヘッダ4バイトの直後から）
    u_int16_t data_addr = pkt_addr + 4;

    // リングバッファのラップアラウンドチェック
    u_int16_t ring_end = PSTOP_ADDR << 8;
    if (data_addr + data_len > ring_end) {
        // ラップアラウンド: 2回に分けて読む
        u_int16_t first_len = ring_end - data_addr;
        read_remote_dma(data_addr, uip_buf, first_len);
        u_int16_t second_addr = PSTART_ADDR << 8;
        read_remote_dma(second_addr, (u_int8_t *)uip_buf + first_len, data_len - first_len);
    } else {
        read_remote_dma(data_addr, uip_buf, data_len);
    }

    // BNRYを更新（NE2000仕様: BNRYはnext_page - 1に設定）
    u_int8_t new_bnry = next_page - 1;
    if (new_bnry < PSTART_ADDR) {
        new_bnry = PSTOP_ADDR - 1;
    }
    out8(io_base + O_BNRY, new_bnry);

    return data_len;
}
```

### RSAR0/RSAR1のアドレス変換に関する注意

現在の `write_remote_dma()` では:
```c
out8(io_base+O_RSAR1, addr & 0xff);   // 低バイト → RSAR1
out8(io_base+O_RSAR0, (addr >> 8));    // 高バイト → RSAR0
```

これは標準NE2000仕様（RSAR0=低バイト、RSAR1=高バイト）と逆に見える。
QEMUのNE2000エミュレーションとの相性の可能性あり。

**重要**: `read_remote_dma()` でも同じパターンに合わせること。
もし送信が既に動いているなら、このパターンがQEMU上では正しい。

## CR_RD2について

受信コードで `CR_RD2`（0x20 = CR_RD_STOP）を使っている箇所がある。
これはページ切り替え時にDMAを停止させるためのフラグ。
ページレジスタにアクセスする前後で使用する。

```c
// ページ切り替え時のパターン
out8(io_base + I_CR, CR_PAGE1 | CR_STA | CR_RD2);  // Page1, Start, DMA停止
// ... Page1レジスタへアクセス ...
out8(io_base + I_CR, CR_PAGE0 | CR_STA | CR_RD2);  // Page0に戻す
```

## テスト

### QEMUでの動作確認

```bash
qemu-system-i386 \
    -fda src/bin/fsboot.bin \
    -net nic,model=ne2k_isa,irq=11,iobase=0xc100 \
    -net user
```

### デバッグ出力の追加

実装初期は各ステップで `_kprintf()` を入れてデバッグ:

```c
PUBLIC int ne2000_receive()
{
    // ...
    _kprintf("ne2000_rx: bnry=%x curr=%x\n", bnry, curr);
    _kprintf("ne2000_rx: status=%x next=%x len=%d\n", recv_status, next_page, pkt_len);
    // ...
}
```

### テストシナリオ

1. QEMUのuser netはARP要求をゲストに送信する → これを受信できるか確認
2. ホストからping → ICMP echo requestパケットが受信できるか確認
3. `_kprintf()` で受信データの先頭をダンプして、正しいEthernetフレームか検証

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `src/drivers/ne2000.c` | `read_remote_dma()` 追加、`ne2000_receive()` 実装 |

## 依存関係

- `uip_buf` と `UIP_BUFSIZE` にアクセスするため `<uip.h>` のインクルードが必要
- `uip_buf` は `uip.c` で定義されたグローバル配列

## 完了条件

- [ ] `read_remote_dma()` がNIC RAMからデータを読み出せる
- [ ] `ne2000_receive()` がリングバッファからパケットを取得し `uip_buf` に格納する
- [ ] リングバッファのラップアラウンドが正しく処理される
- [ ] BNRYが正しく更新される
- [ ] QEMUからのARP/ICMPパケットが `_kprintf()` で確認できる
