# Plan 08: TLS クライアント

## 概要

Plan 02 (HTTP クライアント) + Plan 05 (DNS) + Plan 07 (BearSSL) を統合し、
HTTPS でリクエストを送れる高レベル API を構築する。
証明書ピンニングまたは最小 CA バンドルで `api.anthropic.com` を検証する。

## 目標

- FQDN 指定で HTTPS 接続できる（DNS 解決 → TCP → TLS → HTTP）
- `api.anthropic.com` の証明書を検証できる
- HTTP クライアント (Plan 02) と透過的に統合し、TLS 有無を呼び出し側が選べる
- TLS 失敗時の診断情報を出力する

## 設計

### 高レベル API

```c
/* src/include/tls_client.h */

struct tls_config {
    const unsigned char *trust_anchors;   /* DER エンコードの CA 証明書 */
    int trust_anchors_len;
    int trust_anchors_count;
    int pin_mode;                         /* 1 = ピンニング, 0 = CA 検証 */
    const unsigned char *pinned_pubkey;   /* ピンニング用の公開鍵ハッシュ */
    int pinned_pubkey_len;
};

/* TLS 接続を確立。成功時に tls_connection へのポインタを返す */
int tls_connect(
    const char *hostname,        /* "api.anthropic.com" */
    u_int16_t port,              /* 443 */
    const struct tls_config *cfg,
    struct tls_connection *conn  /* caller-allocated */
);

/* TLS 上でデータ送信 */
int tls_send(struct tls_connection *conn, const char *buf, int len);

/* TLS 上でデータ受信 */
int tls_recv(struct tls_connection *conn, char *buf, int len);

/* TLS 接続を閉じる */
int tls_close(struct tls_connection *conn);
```

### HTTP クライアントとの統合

Plan 02 の `http_do_request()` を拡張し、ポート 443 または明示的 HTTPS 指定時に
TLS 経路を使う:

```c
/* 拡張: TLS を使うフラグを http_request に追加 */
struct http_request {
    /* ... 既存フィールド ... */
    int use_tls;                          /* 1 = HTTPS */
    const struct tls_config *tls_cfg;     /* TLS 設定 */
};

/* http_do_request() 内部:
   if (req->use_tls) {
       dns_resolve(req->host, &dns_result);
       tls_connect(req->host, req->port, req->tls_cfg, &conn);
       tls_send(&conn, request_buf, request_len);
       tls_recv(&conn, response_buf, response_cap);
       tls_close(&conn);
   } else {
       kern_connect(...);
       kern_send(...);
       kern_recv(...);
       kern_close_socket(...);
   }
*/
```

### 証明書検証戦略

**Phase 1: 証明書ピンニング**
- `api.anthropic.com` の公開鍵ハッシュ (SHA-256) をカーネルに埋め込む
- ハンドシェイク時にサーバ証明書の公開鍵ハッシュと比較
- 利点: CA バンドル不要、コードサイズ最小
- 欠点: 証明書ローテーション時にリビルド必要

**Phase 2: 最小 CA バンドル**（将来）
- Let's Encrypt の ISRG Root X1 + DigiCert Global Root G2 程度
- DER エンコードでカーネルイメージに埋め込み（~2KB）

### エラーコード

```c
#define TLS_OK                    0
#define TLS_ERR_DNS              (-1)   /* DNS 解決失敗 */
#define TLS_ERR_TCP              (-2)   /* TCP 接続失敗 */
#define TLS_ERR_HANDSHAKE        (-3)   /* TLS ハンドシェイク失敗 */
#define TLS_ERR_CERT_VERIFY      (-4)   /* 証明書検証失敗 */
#define TLS_ERR_CERT_PIN         (-5)   /* ピンニング不一致 */
#define TLS_ERR_SEND             (-6)   /* 送信失敗 */
#define TLS_ERR_RECV             (-7)   /* 受信失敗 */
#define TLS_ERR_CLOSED           (-8)   /* 相手が切断 */
```

### 診断ログ

```
[TLS] connecting to api.anthropic.com:443
[TLS] DNS resolved: 104.18.x.x (320ms)
[TLS] TCP connected (150ms)
[TLS] handshake: cipher=ChaCha20-Poly1305, version=TLS1.2 (2800ms)
[TLS] cert: CN=api.anthropic.com, issuer=DigiCert
[TLS] cert pin: OK (sha256 match)
[TLS] session established
```

失敗時:
```
[TLS] handshake FAILED: alert=handshake_failure (40)
[TLS] cert verify FAILED: name mismatch (expected api.anthropic.com)
```

## 実装ステップ

1. `tls_client.h` に API とデータ構造を定義する
2. `tls_connect()` を実装する:
   - `dns_resolve()` → `kern_connect()` → BearSSL ハンドシェイク
3. `tls_send()` / `tls_recv()` を BearSSL の `br_sslio_write` / `br_sslio_read` にマップする
4. `tls_close()` で TLS shutdown → TCP close する
5. 証明書ピンニングを実装する（サーバ公開鍵の SHA-256 比較）
6. `api.anthropic.com` の公開鍵ハッシュを取得して埋め込む
7. HTTP クライアントの `http_do_request()` に TLS 分岐を追加する
8. 診断ログをシリアルに出力する

## テスト

### モック結合テスト

- ホスト側で `openssl s_server` を自己署名証明書で起動
- QEMU から TLS 接続 → データ送受信 → 切断
- 証明書不一致で `TLS_ERR_CERT_PIN` が返る

### QEMU スモーク

- `https://10.0.2.2:4443/healthz` （ホスト側 TLS サーバ）に接続
- TLS 上で HTTP GET → 200 応答を受信
- ハンドシェイク時間と暗号スイートをシリアルに出力

### 実 API テスト（手動）

- `api.anthropic.com:443` への TLS ハンドシェイクが成立する
- 証明書ピンニングが通る
- ※ 自動テストでは実 API は使わない（Plan 10 のモックで代用）

## 変更対象

- 新規:
  - `src/net/tls_client.c`
  - `src/include/tls_client.h`
  - `src/net/ca_certs.c` — 埋め込み CA 証明書/ピンデータ
- 既存:
  - `src/net/http_client.c` — TLS 分岐追加
  - `src/include/http_client.h` — use_tls フィールド追加

## 完了条件

- FQDN 指定で HTTPS 接続が成立する
- `api.anthropic.com` の証明書ピンニングが通る
- HTTP クライアントから透過的に TLS を使える
- TLS 失敗時にエラー種別と BearSSL のアラートコードがログに出る
- ハンドシェイク時間が計測できる

## 依存と後続

- 依存: Plan 02 (HTTP), Plan 05 (DNS), Plan 07 (BearSSL)
- 後続: Plan 10 (Claude アダプタ), Plan 11 (ストリーミング結合)

---

## 技術調査結果

### A. TLS 1.2 ハンドシェイク詳細

#### メッセージシーケンス

```
ClientHello (0x01):
  client_version: 0x0303 (TLS 1.2)
  random: 32B (4B Unixtime + 28B 乱数)
  cipher_suites: スイート ID 列
  extensions: SNI, supported_groups, signature_algorithms

ServerHello (0x02):
  random: 32B, cipher_suite: 選択された1つ

Certificate (0x0B):
  DER エンコード X.509 証明書チェーン

ServerKeyExchange (0x0C): ※ECDHE 時のみ
  ECParameters + EC 公開鍵 + 署名

ServerHelloDone (0x0E): 空ボディ

ClientKeyExchange (0x10):
  EC 公開鍵 (ECDHE 時)

ChangeCipherSpec: レコード型 0x14

Finished (0x14):
  verify_data: PRF(master_secret, ...)
```

BearSSL ではこれらすべてが `br_ssl_client_init_full()` + ステートマシンループで自動処理される。

### B. 証明書ピンニングの実装方法

#### 方式比較

| 方式 | メリット | デメリット |
|------|---------|-----------|
| br_x509_knownkey | BearSSL 組み込み、最軽量 | チェーン検証完全スキップ |
| SPKI ピンニング | 鍵が変わらない限り有効 | 抽出がやや複雑 |
| 証明書ハッシュ | 実装が単純 | ローテーションで無効化 |

#### 方式A: br_x509_knownkey_context（推奨）

```c
static const br_ec_public_key KNOWN_KEY = {
    BR_EC_secp256r1,
    (unsigned char *)EC_Q, sizeof EC_Q
};

br_x509_knownkey_context xkc;
br_x509_knownkey_init_ec(&xkc, &KNOWN_KEY, BR_KEYTYPE_EC);
br_ssl_engine_set_x509(&sc.eng, &xkc.vtable);
```

チェーン検証を一切行わず、鍵一致で常に成功する SSH 的モデル。CA バンドル不要。

#### SHA-256 ピンの抽出

```bash
openssl s_client -connect api.anthropic.com:443 -servername api.anthropic.com 2>/dev/null \
  | openssl x509 -pubkey -noout \
  | openssl pkey -pubin -outform der \
  | openssl dgst -sha256 -binary | xxd -p
```

**注意**: EE 証明書の鍵は短期間でローテーション (現在の有効期間は3か月)。中間 CA のピンも併用すべき。

### C. api.anthropic.com の現在の TLS 設定 (2026年3月実測)

#### 証明書チェーン

```
[0] CN=api.anthropic.com
    発行者: Google Trust Services, CN=WE1
    鍵: EC prime256v1 (P-256)
    有効期間: 2026-01-28 〜 2026-04-28

[1] CN=WE1
    発行者: GTS Root R4
    鍵: EC prime256v1 (P-256)

[2] CN=GTS Root R4
    発行者: GlobalSign Root CA
    鍵: EC secp384r1 (P-384)
```

**重要**: DigiCert でも Let's Encrypt でもなく、**Google Trust Services (GTS)** が発行。

#### 暗号スイート

| プロトコル | 暗号スイート | BearSSL 対応 |
|-----------|-------------|-------------|
| TLS 1.3 (デフォルト) | AES_256_GCM_SHA384 | **非対応** |
| TLS 1.2 (フォールバック) | ECDHE-ECDSA-AES128-GCM-SHA256 | **対応** ✓ |
| TLS 1.2 | ECDHE-ECDSA-CHACHA20-POLY1305 | **対応** ✓ |

BearSSL は TLS 1.3 非対応だが、TLS 1.2 もサポートされているため接続可能。サーバ鍵が EC P-256 なので ECDHE_ECDSA 系スイートが必要。

#### 必要な BearSSL スイート設定

```c
static const uint16_t suites[] = {
    BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256, /* 0xCCA9 */
    BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,       /* 0xC02B */
};
```

### D. TLS セッションリサンプション

#### BearSSL API

```c
/* 初回接続 */
br_ssl_client_reset(&sc, "api.anthropic.com", 0);  /* resume=0 */

/* 2回目以降: セッション再利用 */
br_ssl_client_reset(&sc, "api.anthropic.com", 1);  /* resume=1 */
```

フルハンドシェイクの約6往復が2往復に短縮。i486 では ECDHE 鍵交換が数百ms かかるため、セッション再利用の恩恵が大きい。

### 参考資料

- [BearSSL - X.509 Certificates](https://www.bearssl.org/x509.html)
- [BearSSL - bearssl_ssl.h API](https://www.bearssl.org/apidoc/bearssl__ssl_8h.html)
- [SSL Labs - api.anthropic.com](https://www.ssllabs.com/ssltest/analyze.html?d=api.anthropic.com)
