# Plan 01: 能動 TCP 接続の安定化

## 概要

Server runtime では受動 TCP（listen/accept）を安定させた。
Agent 側では能動 TCP（connect）が主経路になる。
`kern_connect()` は存在するが、HTTP クライアントとして連続的に使った実績がないため、
安定性を確認し、足りない部分を補う。

## 目標

- `kern_connect()` で固定 IP:port に TCP 接続して 3-way handshake が完了する
- 接続後に `kern_send()` / `kern_recv()` でデータが往復できる
- `kern_close_socket()` で正常切断し、socket が再利用可能になる
- 接続失敗時（タイムアウト、RST、ARP 未解決）にエラーコードで切り分けできる

## 現状分析

### kern_connect() の実装（socket.c）

```
1. kern_socket() で SOCK_STATE_CREATED
2. uip_connect() で SYN 送信開始
3. ポーリングループ（最大 20M iterations）で CONNECTED 待ち
4. 途中で network_poll() を回して uIP を駆動
```

### 懸念点

- ポーリングの上限 20M は時間ベースではなくイテレーション数。実時間で何秒か不定
- 接続失敗の区別がない（ARP 未解決も RST もすべて同じタイムアウト）
- connect 後の close → 再 connect のサイクルが安定するか未検証
- kern_recv() のポーリング上限 5M も同様の問題

## 設計

### エラーコード追加

```c
/* src/include/socket.h に追加 */
#define SOCK_ERR_TIMEOUT     (-1)   /* 接続タイムアウト */
#define SOCK_ERR_REFUSED     (-2)   /* RST 受信 */
#define SOCK_ERR_ARP_FAIL    (-3)   /* ARP 解決失敗 */
#define SOCK_ERR_NO_SOCKET   (-4)   /* socket 枯渇 */
#define SOCK_ERR_BAD_STATE   (-5)   /* 不正な状態遷移 */
```

### タイムアウトの時間ベース化

PIT tick カウンタを使い、イテレーション数ではなく実時間でタイムアウトする。

```c
#define TCP_CONNECT_TIMEOUT_MS  10000  /* 10秒 */
#define TCP_RECV_TIMEOUT_MS      5000  /* 5秒 */
```

### 接続サイクルテスト

connect → send → recv → close を複数回回し、socket リークがないことを確認する。

## 実装ステップ

1. `kern_connect()` のポーリングを PIT tick ベースのタイムアウトに書き換える
2. uIP の接続状態（`uip_aborted()`, `uip_timedout()`, `uip_closed()`）からエラーコードを区別する
3. `kern_recv()` のポーリングも同様に時間ベースにする
4. `kern_close_socket()` 後の socket 状態が UNUSED に戻ることを確認する
5. connect → send("GET / ...") → recv → close のサイクルを 3 回繰り返すテストを書く
6. シリアルに `TCP_CONNECT: ok`, `TCP_CONNECT: timeout`, `TCP_CONNECT: refused` などを出す

## テスト

### host 側

- socket 状態遷移のユニットテスト（状態を直接操作して検証）
- エラーコードの網羅

### QEMU スモーク

- ホスト側で `nc -l 8080` または Python HTTP サーバを起動
- QEMU 内から `10.0.2.2:8080` に connect → 文字列送信 → 応答受信
- サーバ不在時にタイムアウトエラーが返る
- 3 回連続 connect/close サイクルが通る

## 変更対象

- `src/socket.c` — connect/recv のタイムアウト改善、エラーコード追加
- `src/include/socket.h` — エラーコード定義
- `src/net/netmain.c` — 必要に応じてポーリング改善
- `src/kernel.c` — bring-up テスト用エントリ

## 完了条件

- 固定 IP:port への TCP 接続が 10 秒以内に成功/失敗を返す
- エラー種別（タイムアウト/拒否/ARP 失敗）が区別できる
- connect/close を 3 回繰り返して socket リークしない
- QEMU スモークが 1 コマンドで回せる

## 依存と後続

- 依存: `specs/server-runtime/` (受動 TCP の安定化が前提)
- 後続: Plan 02 (HTTP クライアント), Plan 05 (DNS), Plan 07 (BearSSL)

---

## 技術調査結果

### A. uIP スタックでの Active TCP Connect 詳細

#### uip_connect() の内部動作 (uip.c:406)

1. `lastport` を 4096–31999 の範囲でインクリメントし、既存接続と衝突しないローカルポートを探索
2. `uip_conns[]` 配列から `UIP_CLOSED` 状態のスロットを探す。なければ `UIP_TIME_WAIT` の最古スロットを再利用
3. スロットが見つからなければ `NULL` を返す
4. 初期化:
   - `tcpstateflags = UIP_SYN_SENT` (値: 2)
   - `timer = 1` — **次回の periodicポーリングで SYN を送信**（即座ではない）
   - `rto = UIP_RTO` (値: 3 timer pulses)
   - `nrtx = 0` (再送回数カウンタ)
   - `sa = 0`, `sv = 16` (RTT variance 初期値)

**重要**: `uip_connect()` は SYN を即座に送信しない。次回の `uip_periodic()` 呼び出し時に初めて SYN が生成される。

#### ポーリングモデル (netmain.c)

- `PERIODIC_TIMER_INTERVAL = 50` clock ticks, `CLOCK_CONF_SECOND = 100` → **0.5秒間隔**で periodic 発火
- 発火時に全接続 (`UIP_CONNS=10`) に対して `uip_periodic(i)` を呼び出す
- periodic 内で未ACKデータがある接続の `timer` をデクリメントし、0 になったら再送

#### コールバック状態遷移 (uip-conf.c の uip_appcall)

| フラグ | 発生条件 | Sodex での処理 |
|--------|---------|---------------|
| `uip_connected()` | SYN-ACK 受信後 ESTABLISHED 遷移 | `sk->state = SOCK_STATE_CONNECTED`, `wakeup(&sk->connect_wq)` |
| `uip_closed()` | リモートが FIN で正常切断 | `sk->state = SOCK_STATE_CLOSED`, recv/connect WQ を wakeup |
| `uip_aborted()` | リモートが RST で切断 | 同上 |
| `uip_timedout()` | 再送回数が `UIP_MAXSYNRTX`(5) に達した | 同上 |
| `uip_rexmit()` | 再送が必要 | `sk->tx_buf` の内容を `uip_send()` で再送 |

#### SYN_SENT 状態のパケット処理 (uip.c:1486)

- **SYN+ACK を受信し ACK が正しい場合**: `UIP_ESTABLISHED` に遷移、`UIP_CONNECTED | UIP_NEWDATA` フラグで `UIP_APPCALL()` を呼ぶ
- **それ以外のパケット**: `UIP_ABORT` フラグで `UIP_APPCALL()` を呼び、RST を送信して `UIP_CLOSED` に遷移

#### SYN 再送タイミング

```
timer = UIP_RTO << min(nrtx, 4)
```

| nrtx | timer値 | 実時間(秒) |
|------|---------|-----------|
| 0 | 3 | 1.5s |
| 1 | 6 | 3.0s |
| 2 | 12 | 6.0s |
| 3 | 24 | 12.0s |
| 4+ | 48 | 24.0s |

SYN 再送は最大 `UIP_MAXSYNRTX=5` 回。合計タイムアウト: 約 **46.5秒**

### B. PIT タイマーによるタイムアウト実装

#### Sodex の PIT 設定 (pit8254.c / pit8254.h)

- `CLOCK_TICK_RATE = 1193180` Hz (Intel 8254 発振周波数)
- `HZ = 100` → 10ms 間隔の割り込み
- `LATCH = CLOCK_TICK_RATE / HZ = 11931` (カウンタ初期値)
- Mode 2 (Rate Generator) で動作

#### I/O ポート

| ポート | 名前 | 機能 |
|--------|------|------|
| 0x40 | PIT_COUNTER0 | チャネル0データポート (読み書き) |
| 0x43 | PIT_CONTROL | モード/コマンドレジスタ (書き込みのみ) |

#### ラッチコマンドによるカウンタ読み取り

```c
u_int16_t pit_read_counter0(void) {
    out8(0x43, 0x00);  /* Ch0 ラッチコマンド */
    u_int8_t lo = in8(0x40);
    u_int8_t hi = in8(0x40);
    return (hi << 8) | lo;
}
```

#### tick → ミリ秒変換

```
1 tick = 1000ms / HZ = 1000 / 100 = 10ms
ミリ秒 = kernel_tick * 10
```

uIP の timer ライブラリ: `clock_time()` = `kernel_tick`, `CLOCK_CONF_SECOND = 100`

#### サブティック精度（必要な場合）

```c
u_int16_t current = pit_read_counter0();
u_int32_t elapsed_us = ((LATCH - current) * 1000000UL) / CLOCK_TICK_RATE;
```

**注意**: 現在の `kwait()` はビジーウェイトのみ (pit8254.c:65)。正確なタイムアウトには `kernel_tick` ベースの実装が必要。

### C. TCP 3-way Handshake の失敗モード

#### RST 受信時の uIP の挙動 (uip.c:1384)

uIP は RST の検証を**非常に簡素化**している（コメント: "We do a very naive form of TCP reset processing; we just accept any RST and kill our connection"）。シーケンス番号の検証を行わない。

発生ケース: 相手ポートが listen していない、ファイアウォールが明示的にリジェクト

#### ARP 未解決時の重大問題

`uip_arp_out()` (uip_arp.c:354) の動作が鍵:

ARP テーブルに宛先 MAC がない場合、**元の IP パケット (SYN) を ARP リクエストで上書きする**:

1. uIP が SYN パケットを `uip_buf` に生成
2. `uip_arp_out()` が呼ばれる
3. ARP テーブルにエントリがない → `uip_buf` の中身が ARP リクエストに**置き換えられる**
4. **SYN パケットは消失する**

現在のワークアラウンド (socket.c:557):
```c
if ((poll_count % 500000) == 499999 && syn_retry < 10) {
    uip_periodic_conn(conn);
    ...
}
```

**正しいシーケンス**: ARP request → ARP reply → 次回 periodic で SYN 再送 → SYN-ACK → CONNECTED

#### RFC 6298: TCP 再送タイマー仕様

| 項目 | 仕様値 |
|------|--------|
| 初期 RTO | 1秒 (SHOULD) |
| 最小 RTO | 1秒 |
| バックオフ | `RTO = RTO * 2` (指数) |
| Linux `tcp_syn_retries=6` | 1+2+4+8+16+32 = 約63秒 |
| uIP `UIP_MAXSYNRTX=5` | 1.5+3+6+12+24 = 約46.5秒 |

### D. QEMU User-Mode Networking (SLiRP) での TCP クライアント

#### 仮想ネットワーク構成

| アドレス | 役割 |
|---------|------|
| 10.0.2.0/24 | デフォルトネットワーク |
| 10.0.2.2 | ゲートウェイ兼 DHCP サーバ (= ホスト) |
| 10.0.2.3 | DNS サーバ |
| 10.0.2.15 | ゲストのデフォルト IP |

#### ゲスト→ホスト TCP 接続

**完全にサポートされている**。SLiRP がゲストの TCP パケットを受け取り、ホスト上の `127.0.0.1:port` への実 TCP 接続に変換する。`hostfwd` はホスト→ゲスト方向のポート転送であり、ゲスト→ホスト方向には不要。

接続フロー:
1. ゲストが ARP request (who-has 10.0.2.2) を送信
2. SLiRP が即座に ARP reply を返す（仮想 MAC）
3. ゲストが SYN を送信
4. SLiRP がホスト上で `connect(127.0.0.1, port)` を実行
5. SYN-ACK をゲストに返す → 3-way handshake 完了

#### 制限事項

| 制限 | 詳細 |
|------|------|
| ICMP 不可 | ping は基本不可。10.0.2.2 への echo のみ動作する場合あり |
| TCP/UDP のみ | 他プロトコル非サポート |
| パフォーマンス | ユーザ空間パケット処理のオーバーヘッド |
| 外部アクセス不可 | hostfwd なしではゲストに外部から直接接続不可 |

### E. kern_connect() の現在の問題点まとめ

1. **ビジーウェイトループ**: `poll_count < 20000000` は CPU 周波数依存でタイムアウト時間が不定
2. **割り込み無効化の頻度**: `disableInterrupt()/enableInterrupt()` の高頻度呼び出しが ARP reply 受信を阻害する可能性
3. **ARP→SYN 遷移の不確実性**: `uip_arp_out()` が SYN を ARP で上書きする仕様。ARP 解決後に次回 periodic を待つ必要があり、`poll_count % 500000` のリトライ間隔は CPU 速度依存
4. **`kwait()` のビジーウェイト**: `kernel_tick` ベースのタイムアウト機構が未整備

### 参考資料

- [Contiki 2.6: The uIP TCP/IP stack](https://contiki.sourceforge.net/docs/2.6/a01793.html)
- [RFC 6298 - Computing TCP's Retransmission Timer](https://www.rfc-editor.org/rfc/rfc6298.html)
- [Programmable Interval Timer - OSDev Wiki](https://wiki.osdev.org/Programmable_Interval_Timer)
- [QEMU Network Emulation Documentation](https://qemu.readthedocs.io/en/v10.0.3/system/devices/net.html)
