# Plan 1.5: uIPイベントループの統合

## 概要

NE2000ドライバとuIPスタックを接続するネットワークポーリングループを実装する。
パケット受信→プロトコル処理→応答送信のサイクルを回す。

## 設計

### イベント駆動モデル

uIPはポーリングベースのイベント駆動モデル:

```
[NE2000割り込み] → ne2000_rx_pending = 1
                         ↓
[タイマーorメインループ] → network_poll()
                         ↓
                    ne2000_receive() → uip_buf にパケット格納
                         ↓
                    Ethernetタイプ判定
                    ├── ARP → uip_arp_arpin() → 応答送信
                    └── IP  → uip_input() → TCP/ICMP処理 → 応答送信
                         ↓
                    TCP定期処理（再送タイムアウト等）
                         ↓
                    ARPテーブルエージング
```

### 呼び出しタイミング

2つの方式がある:

**A案: タイマー割り込みから定期呼び出し**
- PITタイマー（100Hz）の割り込みハンドラ内から `network_poll()` を呼ぶ
- メリット: 確実に定期実行される
- デメリット: 割り込みコンテキストでの実行になり、長い処理に不向き

**B案: カーネルのアイドルループから呼び出し**
- プロセススケジューラのアイドル時やメインループから呼ぶ
- メリット: 通常コンテキストで実行、柔軟
- デメリット: 呼び出し頻度がプロセス負荷に依存

**推奨: B案**（アイドルループ）+ タイマーベースの定期処理

実際には、カーネルのメインループ（プロセス実行前の idle 処理等）で
`network_poll()` を頻繁に呼び、内部でタイマーチェックして定期処理を行う。

## 実装

### network_poll() の実装

**ファイル**: 新規 `src/net/netmain.c`

```c
#include <sodex/const.h>
#include <uip.h>
#include <uip_arp.h>
#include <ne2000.h>
#include <timer.h>

// 定期処理の間隔（clock_time単位）
// CLOCK_CONF_SECOND = 100 の場合、50 = 500ms
#define PERIODIC_TIMER_INTERVAL  50
// ARPテーブルエージング間隔（10秒）
#define ARP_TIMER_INTERVAL       (10 * CLOCK_CONF_SECOND)

PRIVATE struct timer periodic_timer;
PRIVATE struct timer arp_timer;
PRIVATE u_int8_t initialized = 0;

PUBLIC void network_init(void)
{
    timer_set(&periodic_timer, PERIODIC_TIMER_INTERVAL);
    timer_set(&arp_timer, ARP_TIMER_INTERVAL);
    initialized = 1;
}

PUBLIC void network_poll(void)
{
    if (!initialized) return;

    int i;

    // 1. 受信パケットの処理
    if (ne2000_rx_pending) {
        ne2000_rx_pending = 0;

        // 複数パケットが溜まっている可能性があるのでループ
        while (1) {
            uip_len = ne2000_receive();
            if (uip_len <= 0) break;

            struct uip_eth_hdr *eth_hdr = (struct uip_eth_hdr *)uip_buf;

            if (eth_hdr->type == htons(UIP_ETHTYPE_IP)) {
                // IPパケット
                uip_arp_ipin();
                uip_input();
                // uip_input()が応答パケットを生成した場合、uip_len > 0
                if (uip_len > 0) {
                    uip_arp_out();
                    ne2000_send(uip_buf, uip_len);
                }
            } else if (eth_hdr->type == htons(UIP_ETHTYPE_ARP)) {
                // ARPパケット
                uip_arp_arpin();
                // ARP応答が生成された場合
                if (uip_len > 0) {
                    ne2000_send(uip_buf, uip_len);
                }
            }
            // それ以外のEtherTypeは無視
        }
    }

    // 2. TCP定期処理（再送、キープアライブ等）
    if (timer_expired(&periodic_timer)) {
        timer_reset(&periodic_timer);

        for (i = 0; i < UIP_CONNS; i++) {
            uip_periodic(i);
            if (uip_len > 0) {
                uip_arp_out();
                ne2000_send(uip_buf, uip_len);
            }
        }

#if UIP_UDP
        // UDP定期処理
        for (i = 0; i < UIP_UDP_CONNS; i++) {
            uip_udp_periodic(i);
            if (uip_len > 0) {
                uip_arp_out();
                ne2000_send(uip_buf, uip_len);
            }
        }
#endif
    }

    // 3. ARPテーブルエージング
    if (timer_expired(&arp_timer)) {
        timer_reset(&arp_timer);
        uip_arp_timer();
    }
}
```

### network_poll() の呼び出し箇所

**ファイル**: `src/kernel.c` または プロセススケジューラ

カーネルのメインループ（アイドル処理）に組み込む:

```c
// start_kernel() の末尾、プロセス実行開始前のループ
// または idle プロセスのループ内
while (1) {
    network_poll();
    // ... プロセススケジューリング等 ...
}
```

あるいは、タイマー割り込みハンドラから軽量な呼び出し:

```c
// PIT割り込みハンドラ内
kernel_tick++;
// 10回に1回（100ms周期）ネットワークポーリング
if (kernel_tick % 10 == 0) {
    network_poll();
}
```

**注意**: 割り込みコンテキストからの呼び出しの場合、`ne2000_send()` 内の
`disableInterrupt()`/`enableInterrupt()` が問題になる可能性あり。
その場合はフラグセットのみ行い、別の場所で処理する。

### network_init() の呼び出し

**ファイル**: `src/kernel.c`

uIP初期化の直後に呼ぶ:

```c
uip_init();
// ... IPアドレス設定 ...
network_init();
_kputs(" KERNEL: SETUP NETWORK\n");
```

### makefile更新

**ファイル**: `src/net/makefile`

`netmain.c` がビルド対象に含まれることを確認。
`*.c` のワイルドカードで自動的に拾われるはずだが、確認が必要。

## uIPのタイマー関数（src/net/timer.c）

既にuIPに含まれているタイマー関数:

```c
void timer_set(struct timer *t, clock_time_t interval);
void timer_reset(struct timer *t);
int timer_expired(struct timer *t);
```

これらは内部で `clock_time()` を呼ぶため、Plan 1.1 の完了が前提。

## htons() について

uIPは `htons()` をマクロで定義している（src/include/uip.h）:
- リトルエンディアン環境ではバイトスワップ
- `UIP_CONF_BYTE_ORDER = LITTLE_ENDIAN` が設定済み

## テスト

### ARP応答テスト

1. QEMUで起動
2. QEMU user netはゲストにARP要求を送信する
3. `network_poll()` がARP要求を受信し、`uip_arp_arpin()` で応答を生成
4. NE2000経由でARP応答を送信
5. QEMUモニタで `info network` を確認

### ping応答テスト

1. QEMU起動オプションに `-net user,hostfwd=...` を追加
2. または `qemu-system-i386 ... -monitor stdio` でQEMUモニタから確認
3. uIPがICMP echo requestを受信し、echo replyを返す
4. ホスト側からは直接pingできない（user netの制限）ため、
   QEMUの `-net tap` オプションを使うか、QEMUモニタで確認

### デバッグ出力

```c
// network_poll() 内の一時的なデバッグ
if (uip_len > 0) {
    struct uip_eth_hdr *eth = (struct uip_eth_hdr *)uip_buf;
    _kprintf("NET RX: type=0x%x len=%d\n", htons(eth->type), uip_len);
}
```

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `src/net/netmain.c` | 新規作成。network_init(), network_poll() |
| `src/kernel.c` | network_init() 呼び出し追加、メインループにnetwork_poll()追加 |
| `src/net/makefile` | 自動で拾われるか確認（ワイルドカード依存） |

## 依存関係

- Plan 1.1（clock_time）: timer_expired() が動くために必須
- Plan 1.2（ne2000_receive）: パケット受信に必須
- Plan 1.3（割り込みハンドラ）: ne2000_rx_pending のセットに必須
- Plan 1.4（カーネル統合）: init_ne2000() と uip_init() に必須

## 完了条件

- [ ] network_poll() がメインループから定期的に呼ばれる
- [ ] 受信パケットがEtherType別に振り分けられる
- [ ] ARP要求に対してARP応答が送信される
- [ ] TCP定期処理が500ms間隔で実行される
- [ ] ARPテーブルエージングが10秒間隔で実行される
