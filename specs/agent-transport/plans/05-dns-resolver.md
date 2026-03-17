# Plan 05: DNS リゾルバ

## 概要

固定 IP の段階を超え、`api.anthropic.com` のような FQDN を解決するために
UDP ベースの最小 DNS クライアントを実装する。

## 目標

- FQDN から IPv4 アドレスを解決できる（A レコード）
- QEMU の user-mode ネットワークの DNS サーバ（10.0.2.3）にクエリを送れる
- タイムアウトと NXDOMAIN を区別できる
- 解決結果をキャッシュできる（TTL 付き、最小限）

## 設計

### DNS パケット構造

RFC 1035 準拠の最小実装:

```
+--+--+--+--+--+--+--+--+--+--+--+--+
|           Header (12 bytes)          |
+--+--+--+--+--+--+--+--+--+--+--+--+
|           Question                   |
+--+--+--+--+--+--+--+--+--+--+--+--+
|           Answer (応答のみ)           |
+--+--+--+--+--+--+--+--+--+--+--+--+
```

### API

```c
/* src/include/dns.h */

#define DNS_SERVER_IP0  10
#define DNS_SERVER_IP1   0
#define DNS_SERVER_IP2   2
#define DNS_SERVER_IP3   3    /* QEMU user-mode の DNS */

#define DNS_PORT         53
#define DNS_MAX_NAME    128
#define DNS_CACHE_SIZE    8   /* キャッシュエントリ数 */
#define DNS_TIMEOUT_MS 3000   /* 3秒タイムアウト */
#define DNS_RETRY_COUNT   2   /* リトライ回数 */

struct dns_result {
    u_int8_t  addr[4];       /* 解決された IPv4 アドレス */
    u_int32_t ttl;           /* キャッシュ有効期間（秒） */
    int       error;         /* 0 = 成功 */
};

/* 名前解決。ブロッキング。キャッシュヒット時は即座に返す */
int dns_resolve(
    const char *hostname,    /* "api.anthropic.com" */
    struct dns_result *out
);

/* キャッシュクリア */
void dns_cache_clear(void);

/* 初期化（DNS サーバ IP 設定） */
void dns_init(void);
```

### エラーコード

```c
#define DNS_OK            0
#define DNS_ERR_TIMEOUT  (-1)  /* 応答なし */
#define DNS_ERR_NXDOMAIN (-2)  /* ドメインが存在しない */
#define DNS_ERR_FORMAT   (-3)  /* 応答パースエラー */
#define DNS_ERR_SEND     (-4)  /* UDP 送信失敗 */
#define DNS_ERR_TOOLONG  (-5)  /* ホスト名が長すぎる */
```

### 実装詳細

**クエリ構築**:
1. Transaction ID をランダム生成（PRNG から 16 bit）
2. Header: QR=0, OPCODE=0, RD=1 (recursion desired)
3. Question: ホスト名をラベル形式にエンコード（`\x03api\x09anthropic\x03com\x00`）
4. Type=A (1), Class=IN (1)

**応答パース**:
1. Transaction ID の一致を確認
2. Header の RCODE を確認（0=成功, 3=NXDOMAIN）
3. Question セクションをスキップ
4. Answer セクションから Type=A のレコードを探す
5. RDATA (4 bytes) から IPv4 アドレスを取得
6. ラベルポインタ（圧縮、0xC0 プレフィックス）に対応する

**キャッシュ**:
```c
struct dns_cache_entry {
    char hostname[DNS_MAX_NAME];
    u_int8_t addr[4];
    u_int32_t expire_tick;    /* PIT tick で有効期限管理 */
    int valid;
};

PRIVATE struct dns_cache_entry dns_cache[DNS_CACHE_SIZE];
```
- LRU は実装せず、最古エントリを上書きする単純な方式
- TTL はサーバ応答値をそのまま使うが、最低 60 秒、最大 300 秒にクリップ

### ネットワーク経路

```
dns_resolve()
  → kern_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
  → kern_sendto(query, DNS_SERVER:53)
  → kern_recvfrom(response, timeout)
  → パース
  → kern_close_socket()
```

## 実装ステップ

1. `dns.h` にデータ構造と API を定義する
2. DNS クエリパケット構築関数を実装する
3. DNS 応答パース関数を実装する（ラベルポインタ対応含む）
4. `dns_resolve()` のメインフローを実装する（送信→受信→パース→キャッシュ）
5. リトライとタイムアウトを実装する
6. `dns_init()` を `kernel.c` の初期化シーケンスに追加する

## テスト

### host 単体テスト (`tests/test_dns_parser.c`)

- クエリ構築:
  - `api.anthropic.com` → 正しいラベルエンコード
  - 長すぎるホスト名 → エラー
- 応答パース:
  - 正常な A レコード応答 → IPv4 取得
  - NXDOMAIN → エラーコード
  - ラベルポインタ（圧縮）を含む応答
  - Answer なし → エラー

### fixture (`tests/fixtures/dns/`)

- `response_a_record.bin` — 正常な A レコード応答（バイナリ）
- `response_nxdomain.bin` — NXDOMAIN 応答
- `response_compressed.bin` — ラベル圧縮を含む応答

### QEMU スモーク

- QEMU user-mode の DNS (10.0.2.3) に `api.anthropic.com` をクエリ
- 解決結果をシリアルに出力
- 存在しないドメインで NXDOMAIN を確認

## 変更対象

- 新規:
  - `src/net/dns.c`
  - `src/include/dns.h`
  - `tests/test_dns_parser.c`
  - `tests/fixtures/dns/`
- 既存:
  - `src/kernel.c` — dns_init() 呼び出し追加

## 完了条件

- `api.anthropic.com` の A レコードを解決できる
- NXDOMAIN とタイムアウトを区別できる
- キャッシュヒット時に再クエリしない
- host 単体テストと QEMU スモークが通る

## 依存と後続

- 依存: Plan 01 (UDP ソケットは TCP 安定化と並行して使えるが、ネットワーク層の安定が前提)
- 後続: Plan 08 (TLS クライアントでホスト名解決に使う)

---

## 技術調査結果

### A. DNS プロトコル詳細 (RFC 1035)

#### DNS ヘッダ構造 (12バイト固定長)

```
オフセット  サイズ  フィールド
0x00       2B     ID        - トランザクション識別子
0x02       2B     Flags     - フラグフィールド
0x04       2B     QDCOUNT   - Question エントリ数
0x06       2B     ANCOUNT   - Answer RR 数
0x08       2B     NSCOUNT   - Authority RR 数
0x0A       2B     ARCOUNT   - Additional RR 数
```

全フィールドはネットワークバイトオーダー（ビッグエンディアン）。Sodex はリトルエンディアン (`UIP_CONF_BYTE_ORDER = LITTLE_ENDIAN`) なので `htons()`/`ntohs()` が必要。

#### Flags ビットレイアウト

```
ビット15:     QR      - 0=クエリ, 1=レスポンス
ビット14-11:  OPCODE  - 0=標準クエリ
ビット10:     AA      - 権威応答
ビット9:      TC      - 切り詰め (512B超で1)
ビット8:      RD      - 再帰要求 (クエリ時に1)
ビット7:      RA      - 再帰利用可能
ビット6-4:    Z       - 予約 (0固定)
ビット3-0:    RCODE   - 0=成功, 3=NXDOMAIN
```

```c
struct dns_header {
    u_int16_t id;
    u_int16_t flags;
    u_int16_t qdcount;
    u_int16_t ancount;
    u_int16_t nscount;
    u_int16_t arcount;
} __attribute__((packed));

#define DNS_FLAG_QR      0x8000
#define DNS_FLAG_RD      0x0100
#define DNS_FLAG_RCODE   0x000F
```

#### ラベルエンコーディング

`api.anthropic.com` のエンコード:
```
03 61 70 69 09 61 6E 74 68 72 6F 70 69 63 03 63 6F 6D 00
|  a  p  i  |  a  n  t  h  r  o  p  i  c  |  c  o  m  |
^           ^                               ^           ^
長さ=3       長さ=9                           長さ=3      終端=0x00
```

- 各ラベル先頭1バイトが長さ (1–63, 上位2ビットは 00)
- 最後は 0x00 で終端
- ドメイン名全体の最大長は 255 オクテット

#### パケット例 (api.anthropic.com の A レコードクエリ)

```
AA BB              ID
01 00              Flags: RD=1
00 01              QDCOUNT=1
00 00 00 00 00 00  ANCOUNT=NSCOUNT=ARCOUNT=0
03 61 70 69 09 ... QNAME
00 01              QTYPE=A(1)
00 01              QCLASS=IN(1)
```

### B. DNS ラベル圧縮（ポインタ方式）

#### ポインタの構造

```
バイト1: 1 1 X X X X X X   ← 上位2ビットが「11」でポインタ
バイト2: X X X X X X X X   ← 下位14ビット全体がオフセット
```

判定:
```c
if ((octet & 0xC0) == 0xC0) {
    u_int16_t offset = ((octet & 0x3F) << 8) | next_octet;
}
```

**重要**: `(octet & 0xC0) == 0xC0` で**両方のビット**が1であることを確認。`if (octet & 0xC0)` は 0x40 や 0x80 を誤判定する典型的バグ (RFC 9267)。

最頻出ポインタ: `C0 0C` (Question の QNAME は常にオフセット12)

#### 循環参照防止策 (RFC 9267 準拠)

1. **前方参照禁止**: ポインタ先はポインタ自身より前であること
2. **ジャンプ回数制限**: 最大 128 回
3. **展開後バッファサイズ制限**: ドメイン名最大 255 オクテット
4. **パケット境界チェック**: オフセットがパケットサイズ内であること

### C. A レコード応答のパース

#### Resource Record フォーマット

```
NAME      可変   ドメイン名 (ラベル or 圧縮ポインタ)
TYPE      2B     RR タイプ
CLASS     2B     RR クラス
TTL       4B     キャッシュ有効期間 (秒)
RDLENGTH  2B     RDATA の長さ
RDATA     可変   リソースデータ
```

A レコード (Type=1, Class=1): RDLENGTH=4, RDATA=IPv4 アドレス (4バイト)

#### パースの注意点

- **TYPE=5 (CNAME)** が返ることがある → CNAME をスキップして A レコードだけ探す
- **RDLENGTH** は必ず検証 (A レコードなら 4)
- 全ポインタ演算でバッファ末尾チェック

### D. QEMU SLiRP の DNS (10.0.2.3)

1. ゲストが `10.0.2.3:53` 宛に UDP DNS クエリを送信
2. SLiRP が受信し、ホスト OS の DNS リゾルバに転送
3. 結果を SLiRP 経由でゲストに返却

`hostfwd` なしでもゲストからの UDP は自動転送される。

#### Sodex に追加すべき定義

```c
#define SODEX_NET_DNS_IP0    10
#define SODEX_NET_DNS_IP1    0
#define SODEX_NET_DNS_IP2    2
#define SODEX_NET_DNS_IP3    3
#define SODEX_NET_DNS_PORT   53
```

### E. uIP での UDP 通信

#### 既存インフラ

- **UDP 有効**: `UIP_CONF_UDP = 1` (uip-conf.h:128)
- **UDP チェックサム有効**: `UIP_CONF_UDP_CHECKSUMS = 1` (同:135)
- **ソケット抽象化**: `kern_sendto()`/`kern_recvfrom()` が socket.c に実装済み
- **UDP コールバック**: `uip_udp_appcall()` → `socket_udp_input()` (uip-conf.c:132)

#### 推奨アプローチ: 既存ソケット層を活用

```c
int dns_resolve(const char *hostname, u_int8_t *ip_out) {
    int sockfd = kern_socket(AF_INET, SOCK_DGRAM, 0);
    /* DNS サーバ 10.0.2.3:53 へクエリ送信 */
    kern_sendto(sockfd, query, qlen, 0, &dns_addr);
    /* kern_recvfrom は内部で network_poll() ループ */
    int rlen = kern_recvfrom(sockfd, response, sizeof(response), 0, NULL);
    /* パース */
    int result = dns_parse_response(response, rlen, ip_out);
    kern_close(sockfd);
    return result;
}
```

`kern_recvfrom()` が `network_poll()` をループで呼ぶ擬似同期モデルを既に実装済みなので、uIP のイベント駆動を意識する必要がない。

#### バッファサイズ

- `UIP_CONF_BUFFER_SIZE = 1500` (Ethernet MTU)
- DNS は通常 512B 以内 (EDNS0 なし) なので十分

### 参考資料

- [RFC 1035 - Domain Names - Implementation and Specification](https://www.rfc-editor.org/rfc/rfc1035)
- [RFC 9267 - Common Implementation Anti-Patterns Related to DNS](https://www.rfc-editor.org/rfc/rfc9267)
- [QEMU Network Emulation - DNS](https://qemu.readthedocs.io/en/v10.0.3/system/devices/net.html)
