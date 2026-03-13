# Plan 1.7: QEMUテスト環境とping疎通確認

## 概要

Plan 1.1〜1.6の実装を統合してQEMU上でテストし、pingが通ることを確認する。
Phase 1全体の最終統合テスト。

## QEMUネットワーク設定

### 起動コマンド

```bash
qemu-system-i386 \
    -fda src/bin/fsboot.bin \
    -net nic,model=ne2k_isa,irq=11,iobase=0xc100,macaddr=52:54:00:12:34:56 \
    -net user \
    -monitor stdio
```

オプション説明:
- `-net nic,model=ne2k_isa`: ISA NE2000互換NICをエミュレート
- `irq=11`: IRQ11（IDTでは0x2B）
- `iobase=0xc100`: I/Oベースアドレス（ne2000.hの`NE2K_QEMU_BASEADDR`と一致）
- `macaddr=52:54:00:12:34:56`: MACアドレス明示指定
- `-net user`: QEMUのユーザーモードネットワーク（NATなし、SLIRP）
- `-monitor stdio`: QEMUモニタを標準入出力に接続（デバッグ用）

### QEMU user netの制限

- ホストからゲストへ直接ping**できない**（SLIRPの制限）
- ゲストからホスト（10.0.2.2）へのTCP接続は可能
- ゲストからインターネットへの接続はNAT経由で可能
- ARPは正常に動作する

### ping疎通の確認方法

#### 方法1: ゲスト→ゲートウェイへのping

Sodex側から10.0.2.2（QEMUゲートウェイ）にICMP echo requestを送信し、
echo replyが返ってくることを確認。

ただしSodexにpingコマンドはまだないので、カーネル内でテストコードを書く:

```c
// kernel.c の start_kernel() 末尾等
// ICMP echo request を手動で構築して送信するテスト
void ping_test(void)
{
    // uIPのICMP処理はecho reply（受信側）のみ対応
    // echo request（送信側）は手動でパケット構築が必要
    // → まずはARP応答テストから始める
}
```

#### 方法2: ARP応答の確認（推奨）

QEMUのuser netは起動時にゲストへARP要求を送信する。
これに対してSodexが正しくARP応答を返せれば、Layer 2/3の通信が成立している。

確認手順:
1. Sodex起動
2. `_kprintf` でARP受信ログを確認
3. QEMUモニタで `info network` を実行し、パケット統計を確認

#### 方法3: tapデバイスを使う（最も確実）

```bash
# tapデバイスのセットアップ（macOS/Linux）
# Linux:
sudo ip tuntap add dev tap0 mode tap
sudo ip addr add 10.0.2.1/24 dev tap0
sudo ip link set tap0 up

# QEMU起動（tap使用）
qemu-system-i386 \
    -fda src/bin/fsboot.bin \
    -net nic,model=ne2k_isa,irq=11,iobase=0xc100 \
    -net tap,ifname=tap0,script=no,downscript=no

# ホストからping
ping 10.0.2.15
```

tapモードではホストから直接pingでき、最も直接的なテストが可能。

## テストシナリオ

### テスト1: NE2000初期化確認

**期待結果**:
```
KERNEL: SETUP NE2000
KERNEL: SETUP uIP
KERNEL: SETUP NETWORK
```
パニックやハングなしで起動完了。

### テスト2: 割り込み受信確認

**期待結果**:
```
NE2000 IRQ: ISR=0x01    ← パケット受信割り込み
```
QEMUのuser netがARP要求を送信し、割り込みが発生する。

### テスト3: パケット受信確認

**期待結果**:
```
ne2000_rx: bnry=46 curr=47
ne2000_rx: status=1 next=47 len=64
NET RX: type=0x806 len=42    ← 0x806 = ARP
```
ARPパケット（EtherType 0x0806）が受信される。

### テスト4: ARP応答確認

**期待結果**:
```
NET RX: type=0x806 len=42    ← ARP要求受信
NET TX: len=42               ← ARP応答送信
```
uip_arp_arpin() がARP応答を生成し、NE2000経由で送信。

### テスト5: ICMP echo reply確認（tap使用時）

**期待結果**:
```
# ホスト側
$ ping 10.0.2.15
PING 10.0.2.15: 64 bytes from 10.0.2.15: icmp_seq=0 ttl=64 time=...
```

## トラブルシューティング

### 割り込みが発生しない

- PICのIMRでIRQ11がマスクされていないか確認
- QEMUの `-net nic` オプションの `irq=11` が正しいか確認
- `init_setupidthandlers()` で `set_trap_gate(0x2B, &asm_i2Bh)` が呼ばれているか確認

### パケット受信で不正なデータが返る

- RSAR0/RSAR1のアドレス設定順序を確認（write_remote_dmaと同じパターン）
- BNRYとCURRの値をダンプして、リングバッファの状態を確認
- QEMUモニタで `xp /16xb 0xc100` 等でNICレジスタを直接確認

### ARP応答が送信されない

- `uip_arp_arpin()` の戻りで `uip_len > 0` か確認
- MACアドレスがNICとuIPで一致しているか確認
- `ne2000_send()` のDMAシーケンスが正しく完了しているか確認

### カーネルがハングする

- `write_remote_dma()` / `read_remote_dma()` のDMA完了待ちが無限ループになっていないか
- `WAIT_MAX_COUNT` の値を確認
- 割り込みの再入が発生していないか（`disableInterrupt()`/`enableInterrupt()` の対称性）

## Phase 1 全体の完了条件

- [ ] clock_time() が正しい時間を返す（Plan 1.1）
- [ ] NE2000がパケットを受信できる（Plan 1.2）
- [ ] 割り込みハンドラが正しく動作する（Plan 1.3）
- [ ] カーネル起動時にNE2000とuIPが初期化される（Plan 1.4）
- [ ] ネットワークポーリングループが動作する（Plan 1.5）
- [ ] tcpip_output()が送信コールバックとして機能する（Plan 1.6）
- [ ] **ARP要求に対してARP応答が返る**
- [ ] **ICMP echo requestに対してecho replyが返る**（tapモード推奨）
