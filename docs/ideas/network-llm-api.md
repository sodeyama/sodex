# Sodex ネットワーク対応 → HTTPSクライアント → ブラウザ

ネットワーク未対応のSodexカーネルを段階的に拡張し、最終的にテキストベースのWebブラウザを実現する計画。

```
Phase 1: ping が通る
Phase 2: TCP 通信ができる
Phase 3: HTTP クライアント（GET/POST）
Phase 4: TLS 対応（HTTPS クライアント）
Phase 5: テキストブラウザ（HTML パース・レンダリング）
```

---

## 現状分析

### 動いているもの
- uIP TCP/IPスタック本体（uip.c）: TCP状態機械、チェックサム計算、ARP処理が実装済み
- NE2000ドライバの送信処理（ne2000_send）: リモートDMAで送信バッファへ書き込み可能
- uIP ARP（uip_arp.c）: ARPテーブル管理、リクエスト/リプライ処理が実装済み

### 動いていないもの
- NE2000ドライバの受信処理: `ne2000_receive()` が空関数
- NE2000割り込みハンドラ: ステータス表示のみで実処理なし
- clock_time(): 常に0を返す（TCP再送タイムアウト等が機能しない）
- tcpip_output(): 空関数（uIPからドライバへの送信コールバック未実装）
- uip_appcall(): 空関数（アプリケーション層のコールバック未実装）
- kernel.cでinit_ne2000()がコメントアウト
- DNS, HTTP, TLS, ソケットAPI: 未実装
- ユーザー空間からのネットワークアクセス: 未実装

### パケットバッファ
- uip_buf: 420バイト（Ethernetヘッダ14 + IP/TCPヘッダ40 + ペイロード最大366バイト）
- HTTP/HTTPS通信にはバッファ拡張が必要（後述）

---

## Phase 1: 基本ネットワーク通信（pingが通る）

### 目標
QEMUの仮想ネットワーク上で、ホストOSからSodexにpingが通ること。

### 1.1 clock_time() の実装

**ファイル**: `src/net/clock-arch.c`

現状: gettimeofday()がコメントアウトされ、常に0を返す。

実装方針:
- カーネルのPITタイマー（timer.c / pit.c）から経過tick数を取得する
- `clock_time_t`として返す（単位: ミリ秒またはtick）
- PITは既にstart_kernel()で初期化済みなので、tickカウンタのextern参照で済む

```c
// clock-arch.c
#include "clock-arch.h"
extern volatile u_int32_t kernel_tick;  // PIT割り込みでインクリメントされるカウンタ

clock_time_t clock_time(void)
{
    return kernel_tick;
}
```

確認事項:
- [ ] PITのtickカウンタがどこで定義されているか確認（timer.c or pit.c）
- [ ] tickの周期確認（10ms? 1ms?）
- [ ] CLOCK_CONF_SECONDをtick周期に合わせて設定

### 1.2 NE2000 受信処理の実装

**ファイル**: `src/drivers/ne2000.c`

現状: `ne2000_receive()` が空関数。

実装方針:
NE2000のリングバッファからパケットを読み出す。

```
NE2000内部メモリレイアウト:
  0x40-0x45: 送信バッファ（6ページ = 1536バイト）
  0x46-0x80: 受信リングバッファ（58ページ = 14848バイト）
```

受信処理の流れ:
1. BNRY（バウンダリ）レジスタとCURR（カレント）レジスタを比較
2. BNRY != CURR なら未読パケットあり
3. リモートDMA読み出しでパケットヘッダ（4バイト: status, next_page, len_lo, len_hi）を取得
4. パケット長に基づいてデータ本体をuip_bufへ読み出し
5. BNRYを更新してリングバッファを進める
6. リングバッファのラップアラウンド処理（PSTOP→PSTARTへ巻き戻し）

```c
PUBLIC int ne2000_receive()
{
    u_int8_t curr, bnry;

    // ページ1に切り替えてCURRを読む
    out8(io_base + I_CR, CR_PAGE1 | CR_STA | CR_RD2);
    curr = in8(io_base + I_CURR);
    out8(io_base + I_CR, CR_PAGE0 | CR_STA | CR_RD2);

    bnry = in8(io_base + I_BNRY);

    if (bnry == curr) {
        return 0;  // 未読パケットなし
    }

    // リモートDMA読み出しでパケットヘッダ取得（4バイト）
    // bnryの指すページ先頭からヘッダを読む
    u_int16_t src_addr = bnry << 8;  // ページ→バイトアドレス変換
    // ... DMA読み出し（ne2000_send()の逆操作）

    // パケットデータをuip_bufへコピー

    // BNRYを更新（next_pageへ）
    // ラップアラウンド: next_page >= PSTOP_ADDR なら PSTART_ADDRへ

    return packet_len;
}
```

確認事項:
- [ ] NE2000のリモートDMA読み出しシーケンスを仕様書で確認
- [ ] ページサイズ: 256バイト（標準NE2000仕様）
- [ ] パケットヘッダ形式: [recv_status][next_page][len_lo][len_hi]

### 1.3 NE2000 割り込みハンドラの実装

**ファイル**: `src/drivers/ne2000.c`

現状: `i2Bh_ne2000_interrupt()` がステータス表示のみ。

実装方針:
- ISR_PRX（受信完了）: ne2000_receive()を呼び出し、受信フラグをセット
- ISR_PTX（送信完了）: 送信完了フラグをセット
- ISR_OVW（オーバーフロー）: リングバッファリセット
- ISRレジスタへの書き戻しで割り込みをクリア

```c
PUBLIC void i2Bh_ne2000_interrupt()
{
    u_int8_t status = in8(io_base + I_ISR);

    if (status & ISR_PRX) {
        ne2000_rx_pending = 1;  // メインループで処理
    }
    if (status & ISR_OVW) {
        // リングバッファオーバーフロー: リセット処理
    }

    // 割り込みクリア
    out8(io_base + I_ISR, status);
    pic_eoi();
}
```

設計判断: 割り込みハンドラ内でuIP処理を直接呼ばない（長時間処理を避ける）。
代わりにフラグをセットし、カーネルのメインループで処理する。

### 1.4 カーネルへの統合

**ファイル**: `src/kernel.c`

実装方針:
1. `init_ne2000()` のコメントアウトを外す
2. NE2000の割り込みをIDTに登録（IRQ 0x2B）
3. uIP初期化: `uip_init()`, IPアドレス設定, MACアドレス設定
4. ネットワークポーリングループの組み込み（タイマー割り込みから定期呼び出し）

```c
// kernel.c: start_kernel() 内
init_ne2000();
uip_init();

uip_ipaddr_t ipaddr;
uip_ipaddr(&ipaddr, 10, 0, 2, 15);    // QEMU user netのゲスト側IP
uip_sethostaddr(&ipaddr);

uip_ipaddr(&ipaddr, 255, 255, 255, 0);
uip_setnetmask(&ipaddr);

uip_ipaddr(&ipaddr, 10, 0, 2, 2);     // QEMUデフォルトゲートウェイ
uip_setdraddr(&ipaddr);
```

### 1.5 uIP イベントループの統合

**ファイル**: 新規 `src/net/netmain.c` または `src/kernel.c` 内

uIPはイベント駆動モデルで、以下のループが必要:

```c
// タイマー割り込み（例: 100ms周期）から呼ばれる
void network_poll(void)
{
    // 受信パケットがあれば処理
    if (ne2000_rx_pending) {
        ne2000_rx_pending = 0;
        uip_len = ne2000_receive();
        if (uip_len > 0) {
            struct uip_eth_hdr *eth = (struct uip_eth_hdr *)uip_buf;
            if (eth->type == htons(UIP_ETHTYPE_IP)) {
                uip_arp_ipin();
                uip_input();
                if (uip_len > 0) {
                    uip_arp_out();
                    ne2000_send(uip_buf, uip_len);
                }
            } else if (eth->type == htons(UIP_ETHTYPE_ARP)) {
                uip_arp_arpin();
                if (uip_len > 0) {
                    ne2000_send(uip_buf, uip_len);
                }
            }
        }
    }

    // TCP定期処理（再送タイムアウト等）
    static int periodic_timer = 0;
    if (++periodic_timer >= PERIODIC_INTERVAL) {
        periodic_timer = 0;
        for (int i = 0; i < UIP_CONNS; i++) {
            uip_periodic(i);
            if (uip_len > 0) {
                uip_arp_out();
                ne2000_send(uip_buf, uip_len);
            }
        }
    }

    // ARPタイマー（10秒周期）
    static int arp_timer = 0;
    if (++arp_timer >= ARP_INTERVAL) {
        arp_timer = 0;
        uip_arp_timer();
    }
}
```

### 1.6 tcpip_output() の実装

**ファイル**: `src/net/uip-conf.c` または新規ファイル

現状: 空関数。uIPがパケットを送信したいとき呼ばれるコールバック。

```c
void tcpip_output(void)
{
    uip_arp_out();
    ne2000_send(uip_buf, uip_len);
}
```

### 1.7 QEMUテスト環境

QEMUでNE2000を有効にする起動コマンド:

```bash
qemu-system-i386 \
    -fda src/bin/fsboot.bin \
    -net nic,model=ne2k_isa,irq=11,iobase=0xc100 \
    -net user,hostfwd=tcp::8080-:80 \
    -serial stdio
```

テスト手順:
1. Sodex起動後、ホストから `ping 10.0.2.15` でICMP応答を確認
2. QEMUモニタで `info network` でパケット統計確認

### Phase 1 完了条件
- [ ] NE2000ドライバで送受信が動作する
- [ ] uIPスタックが統合され、ARP応答ができる
- [ ] ホストOSからpingが通る（ICMP echo reply返却）
- [ ] タイマーが正常動作している（clock_time()が正しい値を返す）

---

## Phase 2: TCP通信

### 目標
Sodexから外部ホストへのTCP接続を確立し、データの送受信ができること。

### 2.1 uip_appcall() の実装

**ファイル**: `src/net/uip-conf.c` または新規 `src/net/app.c`

uIPのアプリケーション層コールバック。接続ごとにイベント（データ受信、ACK、切断等）が通知される。

```c
// 接続ごとのアプリケーション状態
// uip_tcp_appstate_t を構造体に変更（uip-conf.h）
typedef struct {
    u_int8_t  app_id;      // アプリケーション種別
    u_int8_t  state;       // アプリケーション固有状態
    u_int16_t data_len;    // 送信待ちデータ長
    char      *data_ptr;   // 送信待ちデータポインタ
} uip_tcp_appstate_t;

#define APP_NONE    0
#define APP_ECHO    1
#define APP_HTTP    2

void uip_appcall(void)
{
    uip_tcp_appstate_t *app = &uip_conn->appstate;

    switch (app->app_id) {
    case APP_ECHO:
        echo_appcall();
        break;
    case APP_HTTP:
        http_client_appcall();
        break;
    default:
        break;
    }
}
```

### 2.2 echoクライアントの実装（動作確認用）

**ファイル**: 新規 `src/net/echo.c`

最小限のTCPクライアントで、TCP通信の動作確認を行う。

```c
// echoサーバー（ホスト側で nc -l 7 などで起動）に接続して文字列を送受信
void echo_connect(void)
{
    uip_ipaddr_t addr;
    uip_ipaddr(&addr, 10, 0, 2, 2);  // ホスト
    struct uip_conn *conn = uip_connect(&addr, htons(7));
    if (conn) {
        conn->appstate.app_id = APP_ECHO;
        conn->appstate.state = ECHO_CONNECTED;
    }
}

void echo_appcall(void)
{
    if (uip_connected()) {
        uip_send("Hello from Sodex!\n", 18);
    }
    if (uip_newdata()) {
        // 受信データを画面に表示
        char *data = (char *)uip_appdata;
        u_int16_t len = uip_datalen();
        // kprintf等で出力
    }
    if (uip_closed() || uip_aborted() || uip_timedout()) {
        // 切断処理
    }
}
```

### 2.3 パケットバッファの拡張

**ファイル**: `src/include/uip-conf.h`

現在420バイトのバッファはTCPペイロードとして小さすぎる。

```c
// 変更前
#define UIP_CONF_BUFFER_SIZE 420

// 変更後（1500バイト = Ethernet MTU）
#define UIP_CONF_BUFFER_SIZE 1500
```

注意: uip_bufはグローバル配列なのでBSS領域が増える。メモリに余裕があるか確認。

### 2.4 テスト手順

```bash
# ホスト側でechoサーバー起動
nc -l 7

# QEMU起動（ポートフォワード付き）
qemu-system-i386 \
    -fda src/bin/fsboot.bin \
    -net nic,model=ne2k_isa,irq=11,iobase=0xc100 \
    -net user

# Sodex側からecho_connect()を呼び出し
# ncの画面に "Hello from Sodex!" が表示されれば成功
```

### Phase 2 完了条件
- [ ] SodexからホストへTCP接続（3-way handshake）が成功する
- [ ] データ送信: Sodex → ホストへ文字列が届く
- [ ] データ受信: ホスト → Sodexのデータが画面に表示される
- [ ] 接続のクローズが正常に行われる
- [ ] uip_appcall()のディスパッチが動作する

---

## Phase 3: HTTPクライアント

### 目標
SodexからHTTP GETリクエストを送信し、レスポンスを受信・パースできること。
eshellから `wget <url>` のようなコマンドでWebページの内容を取得できること。

### 3.1 DNSリゾルバの実装

**ファイル**: 新規 `src/net/dns.c`, `src/include/dns.h`

HTTPクライアントがホスト名でアクセスするにはDNS名前解決が必要。
まずはIP直指定で動作確認し、その後DNSを実装する。

uIPのUDP機能を有効化する必要あり（`UIP_CONF_UDP = 1`に変更）。

```c
// DNS クエリ送信（UDPポート53）
void dns_query(const char *hostname);

// DNS レスポンスからIPアドレス取得
// コールバック方式: 解決完了時にdns_callback()が呼ばれる
void dns_callback(const char *hostname, uip_ipaddr_t *addr);

// キャッシュ（簡易）: 最大8エントリ
struct dns_entry {
    char hostname[64];
    uip_ipaddr_t addr;
    u_int32_t ttl;
};
```

初期段階では `/etc/hosts` 的な静的テーブルでも代用可能:

```c
// 静的ホスト名テーブル（DNS実装前の代替）
struct static_host {
    const char *name;
    u_int8_t addr[4];
};

PRIVATE struct static_host hosts[] = {
    {"proxy", {10, 0, 2, 2}},
    {NULL, {0}}
};
```

### 3.2 HTTPクライアントの実装

**ファイル**: 新規 `src/net/http_client.c`, `src/include/http_client.h`

HTTP/1.0クライアント（HTTP/1.1のchunked encodingは複雑なので、まずHTTP/1.0で実装）。

```c
// HTTP リクエスト状態
#define HTTP_STATE_IDLE       0
#define HTTP_STATE_CONNECTING 1
#define HTTP_STATE_SENDING    2
#define HTTP_STATE_HEADERS    3
#define HTTP_STATE_BODY       4
#define HTTP_STATE_DONE       5

struct http_request {
    u_int8_t  state;
    char      host[64];
    u_int16_t port;
    char      path[128];
    char      method[8];          // "GET" or "POST"
    char      *body;              // POSTボディ
    u_int16_t body_len;
    u_int16_t status_code;        // HTTPステータスコード
    u_int16_t content_length;     // レスポンスのContent-Length
    u_int16_t received;           // 受信済みバイト数
    char      response_buf[8192]; // レスポンス格納バッファ
};

// HTTP GET
void http_get(const char *host, u_int16_t port, const char *path);

// HTTP POST
void http_post(const char *host, u_int16_t port, const char *path,
               const char *body, u_int16_t body_len);
```

#### リクエスト送信処理

```c
void http_client_appcall(void)
{
    struct http_request *req = /* appstateから取得 */;

    if (uip_connected()) {
        // HTTPリクエストヘッダ組み立て
        // GET /path HTTP/1.0\r\n
        // Host: hostname\r\n
        // User-Agent: Sodex/0.1\r\n
        // Connection: close\r\n
        // \r\n
        req->state = HTTP_STATE_SENDING;
        // uip_send() でリクエスト送信
    }

    if (uip_newdata()) {
        // レスポンスヘッダのパース
        // Content-Length取得
        // ボディの蓄積
    }

    if (uip_closed()) {
        req->state = HTTP_STATE_DONE;
        // コールバックでアプリケーションに通知
    }
}
```

#### レスポンスパーサー

```c
// HTTPレスポンスヘッダの簡易パーサー
// "HTTP/1.0 200 OK\r\n" からステータスコード取得
// "Content-Length: 1234\r\n" からボディ長取得
// "Content-Type: text/html\r\n" からコンテンツタイプ取得
// "\r\n\r\n" でヘッダ終了を検出

int http_parse_status(const char *buf);
int http_parse_content_length(const char *buf);
const char *http_find_body(const char *buf);
```

### 3.3 eshellコマンド

**ファイル**: eshell関連ソース

```c
// HTTP GETでページ取得して表示
// 使い方: wget 10.0.2.2:8080/index.html
void cmd_wget(char *args)
{
    // URLパース（host, port, path）
    // http_get() 呼び出し
    // レスポンスを画面に表示
}
```

### 3.4 テスト手順

```bash
# ホスト側で簡易HTTPサーバー起動
python3 -m http.server 8080

# QEMU起動
qemu-system-i386 \
    -fda src/bin/fsboot.bin \
    -net nic,model=ne2k_isa,irq=11,iobase=0xc100 \
    -net user

# Sodex eshellから:
#   wget 10.0.2.2:8080/index.html
# HTMLソースが表示されれば成功
```

### Phase 3 完了条件
- [ ] HTTP GETリクエストを送信し、レスポンスを受信できる
- [ ] HTTP POSTリクエストを送信できる
- [ ] HTTPレスポンスヘッダをパースしてステータスコード・ボディを抽出できる
- [ ] eshellから `wget` コマンドでページ内容を取得・表示できる
- [ ] DNSリゾルバまたは静的ホストテーブルで名前解決できる

---

## Phase 4: TLS対応（HTTPSクライアント）

### 目標
SodexからHTTPSサイトに直接接続できること。Phase 3のHTTPクライアントの上にTLSレイヤーを追加する。

### 4.1 方針の選択

TLSをゼロから実装するのは大規模な作業。現実的な選択肢:

**A案: 最小限のTLS 1.2クライアント実装**
- 対応する暗号スイートを1つに絞る（例: TLS_RSA_WITH_AES_128_CBC_SHA256）
- 証明書検証は簡略化（ルートCA証明書をカーネルに埋め込み）
- 完全自前実装で外部依存なし

**B案: TLS 1.3のみ対応**
- TLS 1.3はハンドシェイクが簡素化されている
- ただし暗号アルゴリズムの実装量は変わらない

**推奨: A案（TLS 1.2、暗号スイート1つ）** から開始。

### 4.2 必要な暗号アルゴリズムの実装

**ファイル**: 新規 `src/lib/crypto/` ディレクトリ

すべてゼロから実装する（外部ライブラリ不可のため）。

| アルゴリズム | 用途 | ファイル | 難易度 |
|------------|------|---------|--------|
| SHA-256 | ハンドシェイクハッシュ、HMAC | `sha256.c` | 中 |
| AES-128 | データ暗号化（CBC） | `aes.c` | 中 |
| HMAC-SHA256 | MACタグ生成 | `hmac.c` | 低（SHA-256の上に構築） |
| RSA | サーバー証明書検証、鍵交換 | `rsa.c` | 高（多倍長整数演算必要） |
| 多倍長整数 | RSA用の大きな数の演算 | `bignum.c` | 高 |
| PRNG | 乱数生成（ClientHello等） | `random.c` | 中 |

#### SHA-256

```c
// src/lib/crypto/sha256.c
struct sha256_ctx {
    u_int32_t state[8];
    u_int64_t count;
    u_int8_t  buffer[64];
};

void sha256_init(struct sha256_ctx *ctx);
void sha256_update(struct sha256_ctx *ctx, const u_int8_t *data, u_int32_t len);
void sha256_final(struct sha256_ctx *ctx, u_int8_t hash[32]);
```

#### AES-128

```c
// src/lib/crypto/aes.c
struct aes_ctx {
    u_int32_t round_key[44];  // AES-128: 10ラウンド + 1
};

void aes_init(struct aes_ctx *ctx, const u_int8_t key[16]);
void aes_encrypt_block(struct aes_ctx *ctx, const u_int8_t in[16], u_int8_t out[16]);
void aes_decrypt_block(struct aes_ctx *ctx, const u_int8_t in[16], u_int8_t out[16]);

// CBCモード
void aes_cbc_encrypt(struct aes_ctx *ctx, const u_int8_t *iv,
                     const u_int8_t *in, u_int8_t *out, u_int32_t len);
void aes_cbc_decrypt(struct aes_ctx *ctx, const u_int8_t *iv,
                     const u_int8_t *in, u_int8_t *out, u_int32_t len);
```

#### RSAと多倍長整数

```c
// src/lib/crypto/bignum.c
// 2048ビット以上の整数演算（RSA用）
struct bignum {
    u_int32_t *data;
    u_int32_t  size;     // ワード数
};

void bn_mul(struct bignum *r, const struct bignum *a, const struct bignum *b);
void bn_mod(struct bignum *r, const struct bignum *a, const struct bignum *m);
void bn_modexp(struct bignum *r, const struct bignum *base,
               const struct bignum *exp, const struct bignum *mod);

// src/lib/crypto/rsa.c
int rsa_verify_pkcs1(const u_int8_t *sig, u_int32_t sig_len,
                     const u_int8_t *hash, u_int32_t hash_len,
                     const struct rsa_pubkey *key);
```

#### 乱数生成

```c
// src/lib/crypto/random.c
// PITカウンタ、キーボード入力タイミング等からエントロピー収集
void random_init(void);
void random_add_entropy(u_int32_t data);
void random_bytes(u_int8_t *buf, u_int32_t len);
```

### 4.3 TLSレコードプロトコル

**ファイル**: 新規 `src/net/tls.c`, `src/include/tls.h`

```c
// TLSレコード層
#define TLS_CONTENT_HANDSHAKE    22
#define TLS_CONTENT_ALERT        21
#define TLS_CONTENT_CHANGE_SPEC  20
#define TLS_CONTENT_APP_DATA     23

struct tls_record {
    u_int8_t  content_type;
    u_int16_t version;       // 0x0303 = TLS 1.2
    u_int16_t length;
    u_int8_t  fragment[];
};

// TLS接続状態
struct tls_context {
    u_int8_t  state;
    u_int8_t  client_random[32];
    u_int8_t  server_random[32];
    u_int8_t  master_secret[48];
    u_int8_t  client_write_key[16];
    u_int8_t  server_write_key[16];
    u_int8_t  client_write_iv[16];
    u_int8_t  server_write_iv[16];
    struct aes_ctx encrypt_ctx;
    struct aes_ctx decrypt_ctx;
    u_int64_t seq_num_send;
    u_int64_t seq_num_recv;
};
```

### 4.4 TLSハンドシェイク

```
Client                              Server
  |                                    |
  |--- ClientHello ------------------>|  (暗号スイート提案)
  |<-- ServerHello -------------------|  (暗号スイート選択)
  |<-- Certificate -------------------|  (サーバー証明書)
  |<-- ServerHelloDone ---------------|
  |--- ClientKeyExchange ------------>|  (pre-master secret)
  |--- ChangeCipherSpec ------------->|
  |--- Finished --------------------->|
  |<-- ChangeCipherSpec --------------|
  |<-- Finished ----------------------|
  |                                    |
  |=== Application Data (暗号化) =====|
```

各メッセージの組み立て・パースを実装する。

### 4.5 X.509証明書の簡易パーサー

**ファイル**: 新規 `src/lib/crypto/x509.c`

完全なASN.1/DERパーサーは複雑すぎるため、必要最低限の情報だけ抽出:
- サーバーの公開鍵（RSA modulus + exponent）
- 有効期限
- Subject / Issuer

ルートCA証明書はカーネルバイナリに組み込む（Let's Encrypt等の主要CA）。

### 4.6 HTTPSクライアント統合

Phase 3のHTTPクライアントを拡張し、TLSレイヤーを挟む:

```c
// https_get: TLSハンドシェイク → HTTPリクエスト送信（暗号化）→ レスポンス受信（復号）
void https_get(const char *host, const char *path);

// 内部: uip_send()の代わりにtls_send()を使う
void tls_send(struct tls_context *ctx, const u_int8_t *data, u_int16_t len);
u_int16_t tls_recv(struct tls_context *ctx, u_int8_t *data, u_int16_t max_len);
```

### 4.7 メモリ考慮事項

TLS実装は大量のメモリを消費する:

| 用途 | サイズ | 備考 |
|------|--------|------|
| TLSコンテキスト | ~256バイト | 鍵・IV・シーケンス番号 |
| TLSハンドシェイクバッファ | ~4,096バイト | 証明書チェーンが大きい |
| RSA演算用ワーク | ~2,048バイト | 2048ビット鍵の多倍長整数 |
| ルートCA証明書 | ~4,096バイト | 数個のCA証明書をバイナリに埋め込み |

合計: 約10-15KB追加。Phase 3までの6KBと合わせて約20KB。

### Phase 4 完了条件
- [ ] SHA-256, AES-128, HMAC が正しく動作する（テストベクタで検証）
- [ ] RSA署名検証が動作する
- [ ] TLSハンドシェイクが完了する
- [ ] 暗号化されたHTTPリクエスト/レスポンスをやり取りできる
- [ ] eshellから `wget https://...` でHTTPSページを取得できる

---

## Phase 5: テキストブラウザ

### 目標
SodexのVGAテキストモード画面上で、HTMLページの内容を整形表示できるテキストベースのWebブラウザ。
lynxやw3mのようなテキストブラウザを極限まで簡素化したもの。

### 5.1 HTMLパーサー

**ファイル**: 新規 `src/lib/html.c`, `src/include/html.h`

完全なHTML仕様準拠は不要。対応するタグを限定する。

#### 対応タグ（最小限）

| タグ | レンダリング |
|------|------------|
| `<html>`, `<head>`, `<body>` | 構造のみ（headは非表示） |
| `<title>` | タイトルバーに表示 |
| `<h1>`〜`<h6>` | 見出し（太字属性 or 大文字化、前後に空行） |
| `<p>` | 段落（前後に空行） |
| `<br>` | 改行 |
| `<a href="...">` | リンク（[N]番号表示、後でリンク先一覧を表示） |
| `<ul>`, `<ol>`, `<li>` | リスト（`*` や `1.` でインデント表示） |
| `<pre>`, `<code>` | 整形済みテキスト（そのまま表示） |
| `<b>`, `<strong>` | VGA属性で太字 or 色変え |
| `<em>`, `<i>` | VGA属性で色変え |
| `<table>`, `<tr>`, `<td>` | 簡易テーブル（固定幅カラム） |
| `<img>` | `[IMG: alt text]` と表示 |
| `<script>`, `<style>` | 完全に無視 |

#### パーサー設計

SAXライクなストリーミングパーサー（メモリ効率重視）:

```c
// HTMLイベントコールバック
typedef void (*html_tag_handler)(const char *tag, const char *attrs);
typedef void (*html_text_handler)(const char *text, u_int16_t len);

struct html_parser {
    u_int8_t  state;
    html_tag_handler  on_open_tag;
    html_tag_handler  on_close_tag;
    html_text_handler on_text;
    char tag_buf[64];         // 現在のタグ名
    char attr_buf[256];       // 現在の属性文字列
};

void html_parser_init(struct html_parser *p);
void html_parser_feed(struct html_parser *p, const char *data, u_int16_t len);
```

### 5.2 テキストレンダラー

**ファイル**: 新規 `src/usr/browser.c` または `src/browser.c`

HTMLパーサーのイベントを受けて、VGAテキストバッファ（80x25）に整形出力する。

```c
struct browser_state {
    u_int16_t cursor_x, cursor_y;
    u_int16_t scroll_offset;       // スクロール位置
    u_int8_t  in_pre;              // <pre>タグ内か
    u_int8_t  in_head;             // <head>タグ内か（表示しない）
    u_int8_t  bold;                // 太字モード
    u_int8_t  indent_level;        // リストのインデント
    u_int16_t link_count;          // リンク番号カウンタ
    struct {
        char url[256];
    } links[32];                   // リンク先テーブル（最大32個）
    char title[80];                // ページタイトル
    char line_buf[80];             // 行バッファ（ワードラップ用）
    u_int16_t line_pos;
};

// レンダリング関数
void browser_render_text(struct browser_state *bs, const char *text, u_int16_t len);
void browser_render_tag(struct browser_state *bs, const char *tag, const char *attrs);
void browser_newline(struct browser_state *bs);
void browser_flush_line(struct browser_state *bs);
```

#### VGA出力

VGAテキストモード（0xB8000）を直接操作:
- 80x25文字（2000文字）
- 各文字 = 文字コード(1byte) + 属性(1byte)
- 属性: 前景色4bit + 背景色4bit
- スクロール: 画面バッファの内容をシフト

```c
// VGA属性
#define VGA_NORMAL    0x07  // 白on黒
#define VGA_BOLD      0x0F  // 明るい白on黒
#define VGA_LINK      0x09  // 明るい青on黒
#define VGA_HEADING   0x0E  // 黄色on黒
#define VGA_TITLE_BAR 0x70  // 黒on白（反転）
```

### 5.3 ページナビゲーション

```c
// ブラウザコマンド
void browser_open(const char *url);        // URLを開く
void browser_scroll_down(void);            // PageDown
void browser_scroll_up(void);              // PageUp
void browser_follow_link(int link_num);    // リンクをたどる
void browser_back(void);                   // 戻る（履歴スタック）

// URL履歴（簡易スタック）
#define HISTORY_MAX 8
struct {
    char url[256];
} history[HISTORY_MAX];
u_int8_t history_pos;
```

### 5.4 URLパーサー

```c
// "http://host:port/path" を分解
struct parsed_url {
    char scheme[8];       // "http" or "https"
    char host[64];
    u_int16_t port;       // デフォルト: http=80, https=443
    char path[128];
};

int url_parse(const char *url, struct parsed_url *out);
```

### 5.5 eshellコマンド

```c
// ブラウザ起動
// 使い方: browse http://example.com/
void cmd_browse(char *args)
{
    browser_open(args);
    // キー入力ループ:
    //   j/k or ↑↓: スクロール
    //   数字+Enter: リンクをたどる
    //   b: 戻る
    //   q: 終了
}
```

### 5.6 画面レイアウト例

```
+------------------------- Sodex Browser --------------------------+
| Example Page                                          [http://..] |
+-------------------------------------------------------------------+
|                                                                   |
|  Welcome to Sodex Browser                                         |
|  ========================                                         |
|                                                                   |
|  This is a paragraph of text that wraps at 80 columns. The        |
|  browser renders HTML into plain text with basic formatting.      |
|                                                                   |
|  Links:                                                           |
|    * [1] About page                                               |
|    * [2] Documentation                                            |
|                                                                   |
|  Preformatted code:                                               |
|  +---------------------------------------------------------------+|
|  | void main() {                                                 ||
|  |     printf("hello");                                          ||
|  | }                                                             ||
|  +---------------------------------------------------------------+|
|                                                                   |
+-------------------------------------------------------------------+
| [j]Down [k]Up [N]Follow link [b]Back [q]Quit        Line: 1/42   |
+-------------------------------------------------------------------+
```

### 5.7 メモリ考慮事項

| 用途 | サイズ | 備考 |
|------|--------|------|
| HTMLパーサー状態 | ~512バイト | タグ/属性バッファ |
| ブラウザ状態 | ~10KB | リンクテーブル、行バッファ、履歴 |
| ページバッファ | ~32KB | レンダリング済みテキスト（スクロール用） |
| HTTPレスポンス | ~16KB | HTMLソース格納 |

合計: 約60KB（Phase 1-4含む）。カーネルのメモリアロケータで確保。

### Phase 5 完了条件
- [ ] HTMLをパースして主要なタグを認識できる
- [ ] VGAテキスト画面にHTMLを整形表示できる
- [ ] リンク番号表示とリンクナビゲーションが動作する
- [ ] スクロール（上下）が動作する
- [ ] 履歴（戻る）が動作する
- [ ] eshellから `browse <url>` でWebページを閲覧できる

---

## 補足: ファイル変更一覧

### 既存ファイルの修正

| ファイル | Phase | 変更内容 |
|---------|-------|---------|
| `src/net/clock-arch.c` | 1 | clock_time()をPITベースに実装 |
| `src/drivers/ne2000.c` | 1 | ne2000_receive()実装、割り込みハンドラ実装 |
| `src/kernel.c` | 1 | init_ne2000()有効化、uIP初期化、ネットワークループ統合 |
| `src/net/uip-conf.c` | 1 | tcpip_output()実装 |
| `src/include/uip-conf.h` | 2 | uip_tcp_appstate_t拡張、バッファサイズ変更、UDP有効化 |

### 新規ファイル

| ファイル | Phase | 内容 |
|---------|-------|------|
| `src/net/netmain.c` | 1 | ネットワークポーリングループ |
| `src/net/echo.c` | 2 | echoクライアント（テスト用） |
| `src/net/dns.c` | 3 | DNSリゾルバ |
| `src/include/dns.h` | 3 | DNSヘッダ |
| `src/net/http_client.c` | 3 | HTTPクライアント |
| `src/include/http_client.h` | 3 | HTTPクライアントヘッダ |
| `src/lib/crypto/sha256.c` | 4 | SHA-256ハッシュ |
| `src/lib/crypto/aes.c` | 4 | AES-128暗号化 |
| `src/lib/crypto/hmac.c` | 4 | HMAC-SHA256 |
| `src/lib/crypto/bignum.c` | 4 | 多倍長整数演算 |
| `src/lib/crypto/rsa.c` | 4 | RSA署名検証 |
| `src/lib/crypto/random.c` | 4 | 疑似乱数生成 |
| `src/lib/crypto/x509.c` | 4 | X.509証明書パーサー |
| `src/net/tls.c` | 4 | TLS 1.2クライアント |
| `src/include/tls.h` | 4 | TLSヘッダ |
| `src/lib/html.c` | 5 | HTMLパーサー |
| `src/include/html.h` | 5 | HTMLパーサーヘッダ |
| `src/browser.c` | 5 | テキストブラウザ |
| `src/include/browser.h` | 5 | ブラウザヘッダ |

---

## リスクと対策

| リスク | 影響 | 対策 |
|--------|------|------|
| NE2000受信でリングバッファ管理バグ | Phase 1停止 | QEMUモニタでパケットダンプしてデバッグ |
| uIPのバッファ420バイトでHTTP不可 | Phase 3停止 | 1500バイトに拡張（メモリ確認） |
| TCPの再送が頻発 | 通信不安定 | clock_time()の精度確認、タイムアウト値調整 |
| RSA多倍長整数演算が遅すぎる | TLSハンドシェイクタイムアウト | Montgomery乗算で最適化、サーバー側のタイムアウトを延長 |
| TLSハンドシェイクのメモリ不足 | Phase 4停止 | 証明書チェーンの分割読み込み、バッファの動的確保 |
| HTML仕様の複雑さ | Phase 5の品質低下 | 対応タグを厳選、壊れたHTMLは最善努力で表示 |
| QEMUのNE2000エミュレーションの癖 | ドライババグに見える | QEMU側のソースやドキュメント参照 |
