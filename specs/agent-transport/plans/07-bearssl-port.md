# Plan 07: BearSSL 移植

## 概要

HTTPS 通信に必要な TLS 1.2 を BearSSL で実現する。
BearSSL は malloc 不要・caller-allocated バッファ設計のため、
フリースタンディング環境に最も適した TLS ライブラリ。

## 目標

- BearSSL のソースを Sodex にクロスコンパイルできる
- BearSSL が要求する libc 互換関数を用意する
- uIP TCP の I/O を BearSSL の read/write コールバックに接続する
- TLS 1.2 ハンドシェイクが成立する
- ChaCha20-Poly1305 暗号スイートが動作する

## BearSSL の特性

| 特性 | 詳細 |
|------|------|
| malloc 不要 | すべてのバッファは caller が確保して渡す |
| コードサイズ | 最小構成で約 20–40 KB（暗号スイート選択で変動） |
| RAM 使用 | I/O バッファ約 16 KB（送信 + 受信） |
| 暗号スイート | ChaCha20-Poly1305, AES-GCM, AES-CBC 等 |
| 証明書検証 | X.509 minimal / 完全版の選択可 |
| 対応 TLS | 1.0, 1.1, 1.2 |

## 設計

### ビルド構成

BearSSL のソースから Sodex に必要な最小サブセットを抽出する:

```
src/lib/bearssl/
├── inc/           ← BearSSL の公開ヘッダ（bearssl.h 等）
├── src/
│   ├── codec/     ← PEM, X.509 デコーダ
│   ├── ec/        ← 楕円曲線（Curve25519 用）
│   ├── hash/      ← SHA-256 等（Sodex 既存と重複チェック）
│   ├── int/       ← 大整数演算
│   ├── mac/       ← HMAC
│   ├── rand/      ← PRNG (Sodex 側で差し替え)
│   ├── rsa/       ← RSA（証明書検証に必要）
│   ├── ssl/       ← TLS エンジン本体
│   ├── symcipher/ ← ChaCha20, AES
│   └── x509/      ← 証明書検証
└── sodex_stubs.c  ← Sodex 固有の libc スタブ
```

### libc スタブ

BearSSL が使用する libc 関数を洗い出して補う:

```c
/* src/lib/bearssl/sodex_stubs.c */

/* BearSSL が必要とする関数 */
void *memcpy(void *dst, const void *src, size_t n);   /* 既存あり */
void *memmove(void *dst, const void *src, size_t n);  /* 追加が必要 */
void *memset(void *s, int c, size_t n);                /* 既存あり */
int   memcmp(const void *s1, const void *s2, size_t n); /* 既存あり */
size_t strlen(const char *s);                          /* 既存あり */
```

**memmove の追加**: BearSSL はバッファのオーバーラップ移動に `memmove` を使う。
カーネル空間に `memmove` を追加する（ユーザ空間の `src/usr/lib/libc/memory.c` にはあるが、カーネル側にない）。

### I/O コールバック

BearSSL の `br_sslio_context` は low-level read/write コールバックを要求する。
uIP のソケットをこれに接続する:

```c
/* src/net/tls_client.c */

struct tls_connection {
    br_ssl_client_context sc;          /* BearSSL クライアントコンテキスト */
    br_x509_minimal_context xc;        /* 証明書検証コンテキスト */
    br_sslio_context ioc;              /* I/O コンテキスト */
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI]; /* I/O バッファ ~16KB */
    int sockfd;                        /* Sodex ソケット */
};

/* BearSSL → uIP: 送信 */
PRIVATE int tls_low_write(void *ctx, const unsigned char *buf, size_t len)
{
    struct tls_connection *tc = ctx;
    return kern_send(tc->sockfd, buf, len, 0);
}

/* BearSSL ← uIP: 受信 */
PRIVATE int tls_low_read(void *ctx, unsigned char *buf, size_t len)
{
    struct tls_connection *tc = ctx;
    return kern_recv(tc->sockfd, buf, len, 0);
}
```

### PRNG 接続

BearSSL に Plan 06 の PRNG を供給する:

```c
br_hmac_drbg_context rng_ctx;
br_hmac_drbg_init(&rng_ctx, &br_sha256_vtable, seed, seed_len);
br_ssl_engine_set_default_rsavrfy(&sc.eng);
br_ssl_engine_set_prf_sha256(&sc.eng);
br_ssl_client_set_default_rsapub(&sc);
```

### 暗号スイート選択

i486 には AES-NI がないため、ChaCha20-Poly1305 を最優先にする:

```c
/* TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256 を最優先 */
static const uint16_t suites[] = {
    BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
    BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    /* AES-GCM はフォールバック */
    BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    0
};
```

### メモリ予算

| コンポーネント | サイズ | 備考 |
|-------------|--------|------|
| BearSSL I/O バッファ | ~16 KB | 送受信それぞれ |
| BearSSL コンテキスト | ~2 KB | sc + xc |
| CA 証明書バンドル | ~2–4 KB | ピンニングなら 1 証明書分 |
| **合計** | **~22 KB** | 1 接続あたり |

## 実装ステップ

1. BearSSL ソースをダウンロードし、`src/lib/bearssl/` に最小サブセットを配置する
2. BearSSL 用の makefile.inc を作成する（クロスコンパイルフラグ設定）
3. `sodex_stubs.c` に libc スタブを実装する（特に `memmove`）
4. `memmove` をカーネル空間の `src/lib/` にも追加する
5. BearSSL を `-m32 -nostdlib -ffreestanding` でコンパイルが通るまで調整する
6. PRNG コールバックを Plan 06 の `prng_bytes()` に接続する
7. I/O コールバックを `kern_send()` / `kern_recv()` に接続する
8. テスト用にホスト側で `openssl s_server` を立て、TLS ハンドシェイクを通す

## テスト

### ビルドテスト

- `make` で BearSSL を含むカーネルがリンクエラーなくビルドできる
- 未解決シンボルがない

### QEMU スモーク

- ホスト側で TLS サーバ（自己署名証明書）を起動
- QEMU 内から接続し、TLS ハンドシェイクが完了する
- ハンドシェイク後に平文データが送受信できる
- 証明書検証失敗時にエラーを返す

### パフォーマンス

- TLS ハンドシェイクが 30 秒以内に完了する（i486 の速度制約）
- ChaCha20-Poly1305 が選択されていることをログで確認

## 変更対象

- 新規:
  - `src/lib/bearssl/` — BearSSL ソース最小サブセット
  - `src/lib/bearssl/sodex_stubs.c` — libc スタブ
  - `src/lib/bearssl/makefile.inc` — ビルド設定
  - `src/lib/memmove.c` — カーネル空間 memmove
- 既存:
  - `makefile.inc` — BearSSL のインクルードパスとリンク
  - `src/lib/makefile` — BearSSL サブディレクトリ追加

## 完了条件

- BearSSL が Sodex のツールチェーンでコンパイルできる
- libc スタブで未解決シンボルがない
- I/O コールバックが uIP ソケットに接続されている
- TLS ハンドシェイクが QEMU 上で成立する
- SSH の既存機能が壊れない

## 依存と後続

- 依存: Plan 01 (TCP), Plan 06 (PRNG)
- 後続: Plan 08 (TLS クライアント)

## リスク

| リスク | 影響 | 対策 |
|--------|------|------|
| BearSSL のコードサイズがカーネルイメージに収まらない | ブート失敗 | 不要な暗号スイートを除外して最小化 |
| memmove 以外にも未知の libc 依存がある | ビルド失敗 | BearSSL のソースを grep して依存関数を網羅的に洗う |
| i486 での TLS ハンドシェイクが遅すぎる | タイムアウト | ChaCha20 優先、TLS session resumption を検討 |
| BearSSL のバッファ 16KB がメモリ予算を圧迫 | 他機能への影響 | I/O バッファサイズを BR_SSL_BUFSIZE_MONO (~6KB) に縮小検討 |

---

## 技術調査結果

### A. BearSSL の設計と移植性

#### malloc 不要設計の詳細

- BearSSL には malloc 呼び出しが**一切ない**
- すべてのコンテキスト構造体は呼び出し側がスタックまたは静的領域に確保
- 「成長するバッファ」を使わないストリーム設計
- スレッドも不要（ステートマシン API）

#### br_ssl_client_context の使い方

```c
br_ssl_client_context sc;        /* 約3–4KB */
br_x509_minimal_context xc;

/* フルプロファイル初期化 */
br_ssl_client_init_full(&sc, &xc, TAs, TAs_NUM);

/* 接続リセット（SNI サーバ名指定） */
br_ssl_client_reset(&sc, "api.anthropic.com", 0);
```

#### br_sslio_context の read/write コールバック

```c
br_sslio_context sio;
br_sslio_init(&sio, &sc.eng,
    socket_read, &fd,     /* read callback + context */
    socket_write, &fd);   /* write callback + context */

br_sslio_read(&sio, buf, len);
br_sslio_write_all(&sio, data, data_len);
br_sslio_flush(&sio);
br_sslio_close(&sio);
```

#### 低レベルステートマシン API（コールバック不使用時、推奨）

フリースタンディング環境では簡易 I/O の代わりにステートマシン方式が適している:

```c
for (;;) {
    unsigned state = br_ssl_engine_current_state(&sc.eng);
    if (state & BR_SSL_CLOSED) break;
    if (state & BR_SSL_SENDREC) {
        /* 暗号化レコードを TCP に送信 */
        unsigned char *buf;
        size_t len;
        buf = br_ssl_engine_sendrec_buf(&sc.eng, &len);
        ssize_t wlen = tcp_send(fd, buf, len);
        br_ssl_engine_sendrec_ack(&sc.eng, wlen);
    }
    if (state & BR_SSL_RECVREC) {
        /* TCP から暗号化レコードを受信してエンジンに注入 */
        buf = br_ssl_engine_recvrec_buf(&sc.eng, &len);
        ssize_t rlen = tcp_recv(fd, buf, len);
        br_ssl_engine_recvrec_ack(&sc.eng, rlen);
    }
    if (state & BR_SSL_SENDAPP) {
        /* アプリケーションデータ送信可能 */
        buf = br_ssl_engine_sendapp_buf(&sc.eng, &len);
        memcpy(buf, http_req, req_len);
        br_ssl_engine_sendapp_ack(&sc.eng, req_len);
        br_ssl_engine_flush(&sc.eng, 0);
    }
    if (state & BR_SSL_RECVAPP) {
        /* 復号済みアプリケーションデータ受信 */
        buf = br_ssl_engine_recvapp_buf(&sc.eng, &len);
        process_response(buf, len);
        br_ssl_engine_recvapp_ack(&sc.eng, len);
    }
}
```

### B. libc 依存関数の完全リスト

**公式に明記されている依存（4つ）**: `memcpy`, `memmove`, `memcmp`, `strlen`

**実質的に必要（5つ目）**: `memset` — GCC が `-ffreestanding` でも構造体初期化で暗黙に生成する

**Sodex の対応状況**:

| 関数 | 場所 | 状態 |
|------|------|------|
| `memcpy` | src/sys_core.S | アセンブリ実装あり ✓ |
| `memset` | src/sys_core.S | アセンブリ実装あり ✓ |
| `memmove` | src/lib/string.c | 実装あり ✓ |
| `memcmp` | src/lib/string.c | 実装あり ✓ |
| `strlen` | src/lib/string.c | 実装あり ✓ |

**結論**: 追加の libc スタブは不要。全依存関数が Sodex に既存。

### C. ChaCha20-Poly1305 on i486

#### パフォーマンス比較 (BearSSL i386 32ビット)

| アルゴリズム | 速度 |
|---|---|
| ChaCha20 (ct) | 270.72 MB/s |
| AES-128-CBC encrypt (big) | 135.58 MB/s |

AES-NI なしの i386 では **ChaCha20 は AES-CBC の約2倍高速**。AES-GCM は GHASH のオーバーヘッドがさらにかかるため、**ChaCha20-Poly1305 は AES-GCM の 3–4倍高速**。

#### i486 での TLS ハンドシェイク推定時間

BearSSL ベンチマーク (3.1GHz Xeon, i386モード):
- ECDHE P-256: 0.94ms/回
- RSA-2048 検証: 0.66ms/回
- Curve25519: 0.33ms/回

i486 (25–100MHz) は 30–120倍遅い:

| 操作 | i486 100MHz | i486 25MHz |
|---|---|---|
| ECDHE P-256 | ~29ms | ~117ms |
| RSA-2048 検証 | ~20ms | ~82ms |
| TLS ハンドシェイク全体 (ECDHE) | ~150ms | ~600ms |

### D. BearSSL の最小構成

#### コードサイズ見積もり (i386 32ビット)

| コンポーネント | サイズ |
|---|---|
| SSL エンジン本体 | ~6,100 B |
| クライアントハンドシェイク | ~10,973 B |
| SHA-256 | ~2,640 B |
| ChaCha20 | ~1,047 B |
| Poly1305 | ~1,151–3,697 B |
| HMAC-DRBG | ~1,095 B |
| **最小クライアント合計** | **約25–30 KB** |

#### I/O バッファサイズ

| モード | 定義 | サイズ |
|---|---|---|
| 全二重 (BIDI) | `BR_SSL_BUFSIZE_BIDI` | 33,178 B |
| 半二重 (MONO) | `BR_SSL_BUFSIZE_MONO` | 16,709 B |
| 最小半二重 | MFL 512 | 837 B |

**推奨**: 半二重 `BR_SSL_BUFSIZE_MONO` (16,709B)。メモリが厳しければ MFL 拡張で 837B まで削減可能（サーバ側の MFL 対応が必要）。

### E. BearSSL の PRNG インターフェース

#### br_prng_class 仮想関数テーブル

```c
struct br_prng_class_ {
    size_t context_size;
    void (*init)(const br_prng_class **ctx,
                 const void *params,
                 const void *seed, size_t seed_len);
    void (*generate)(const br_prng_class **ctx,
                     void *out, size_t len);
    void (*update)(const br_prng_class **ctx,
                   const void *seed, size_t seed_len);
};
```

#### br_hmac_drbg_context の使い方

```c
br_hmac_drbg_context rng;
br_hmac_drbg_init(&rng, &br_sha256_vtable, seed_data, seed_len);

unsigned char random_bytes[32];
br_hmac_drbg_generate(&rng, random_bytes, sizeof(random_bytes));

/* 追加エントロピー注入（既存エントロピーを劣化させない、加算的） */
br_hmac_drbg_update(&rng, new_entropy, entropy_len);
```

#### Sodex 向けシーダー実装

```c
int my_seeder(const br_prng_class **ctx) {
    unsigned char seed[32];
    collect_pit_entropy(seed, 32);  /* PIT ジッタから収集 */
    (*ctx)->update(ctx, seed, sizeof(seed));
    return 1;  /* 成功 */
}
```

`update()` はエントロピーを加算的に注入する（既存を劣化させない）。低品質データを渡しても安全。

### 参考資料

- [BearSSL - Project Goals](https://bearssl.org/goals.html)
- [BearSSL - API Overview](https://www.bearssl.org/api1.html)
- [BearSSL - Speed Benchmarks](https://www.bearssl.org/speed.html)
- [BearSSL - Size Calculator](https://www.bearssl.org/sizes.html)
- [BearSSL - bearssl_rand.h API](https://bearssl.org/apidoc/bearssl__rand_8h.html)
