# Plan 06: エントロピーと PRNG

## 概要

TLS ハンドシェイクには暗号学的に安全な乱数が必須。
SSH 用に `ssh_crypto.c` に AES-CTR PRNG があるが、シードの品質が不十分。
PIT タイマージッタ等からエントロピーを収集し、PRNG を強化する。

## 目標

- PIT 割り込みタイミングのジッタからエントロピーを収集できる
- キーボード/ネットワーク割り込みのタイミングも補助エントロピーに使える
- 32 バイトの高品質シードで AES-CTR PRNG を初期化できる
- BearSSL に渡すエントロピーコールバックを提供できる

## 現状分析

### 既存の PRNG (ssh_crypto.c)

```c
static u_int8_t prng_key[16];    /* AES-128 鍵 */
static u_int8_t prng_ctr[16];    /* カウンタ */
static int prng_seeded = 0;

void ssh_prng_seed(const u_int8_t *seed, int len);
void ssh_prng_bytes(u_int8_t *out, int len);
```

- AES-128-CTR ベース。暗号的に安全な構造
- 問題: シードが固定値または外部注入前提。起動時のエントロピー収集がない

### QEMU 環境のエントロピー源

| 源 | 品質 | 取得方法 |
|---|---|---|
| PIT カウンタ (0x40) | 中 | ポート I/O で現在カウント値を読む |
| TSC (rdtsc) | 高だが i486 では不可 | i486 に TSC はない |
| キーボード割り込みタイミング | 低〜中 | 割り込み時の PIT カウンタ |
| NE2000 パケット到着タイミング | 低〜中 | 同上 |
| PIT ジッタ (連続読み取り差分) | 中 | 2 回の PIT 読み取りの LSB 差分 |

## 設計

### エントロピープール

```c
/* src/include/entropy.h */

#define ENTROPY_POOL_SIZE  64   /* バイト */

struct entropy_pool {
    u_int8_t pool[ENTROPY_POOL_SIZE];
    int collected_bits;          /* 推定エントロピービット数 */
    int write_pos;               /* 次の書き込み位置 */
};

/* エントロピー追加（XOR で混合） */
void entropy_add(u_int8_t byte, int estimated_bits);

/* プールが十分なエントロピーを持つか */
int entropy_ready(void);   /* collected_bits >= 256 で true */

/* プールから PRNG シードを取得（SHA-256 でプールを縮約） */
void entropy_get_seed(u_int8_t *seed, int len);
```

### エントロピー収集ポイント

1. **PIT 割り込みハンドラ** (毎ティック):
   - PIT カウンタの LSB を `entropy_add()` に投入
   - 推定 1 bit/tick

2. **キーボード割り込み**:
   - 割り込み時の PIT カウンタ値を投入
   - 推定 4 bits/event

3. **NE2000 パケット受信割り込み**:
   - パケット到着時の PIT カウンタ値を投入
   - 推定 2 bits/event

4. **起動時ジッタ収集**:
   - カーネル初期化中に PIT を 256 回連続読み取り
   - 各読み取りの差分 LSB を投入
   - 推定 128 bits（256 回 × 0.5 bit）

### PRNG の統合

```c
/* src/include/prng.h */

/* 初期化: エントロピープールからシード取得 → AES-CTR PRNG セットアップ */
int prng_init(void);  /* entropy_ready() = false なら -1 */

/* 乱数生成 */
void prng_bytes(u_int8_t *out, int len);

/* BearSSL 用コールバック（br_prng_class 互換） */
/* BearSSL 移植時に接続する */
```

### 既存 ssh_prng との関係

- `ssh_prng_*` を `prng_*` にリネームまたはラップする
- SSH とエージェントで同一の PRNG を共有する
- シードソースをエントロピープールに統一する

## 実装ステップ

1. `entropy.h` / `entropy.c` を実装する（プール、XOR 混合、SHA-256 縮約）
2. PIT 割り込みハンドラにエントロピー収集を追加する
3. 起動時ジッタ収集を `kernel.c` の初期化シーケンスに入れる
4. `prng.h` / `prng.c` で PRNG を統合する
5. `ssh_prng_*` を新 PRNG にリダイレクトする（既存 SSH 機能の互換維持）
6. シリアルにエントロピービット数と PRNG 状態を出力する

## テスト

### host 単体テスト

- エントロピープールの XOR 混合が正しい
- SHA-256 縮約が既知入力で既知出力を返す
- PRNG が同じシードから同じ列を生成する（決定性テスト）
- PRNG の出力が 0 バイアスでない（簡易統計テスト）

### QEMU スモーク

- 起動後にエントロピービット数をシリアルに出力
- `entropy_ready()` が true になるまでの時間を計測
- PRNG から 32 バイト生成してシリアルに hex dump

## 変更対象

- 新規:
  - `src/lib/entropy.c`
  - `src/include/entropy.h`
  - `src/lib/prng.c`
  - `src/include/prng.h`
- 既存:
  - `src/pit.c` — エントロピー収集追加
  - `src/keyboard.c` — エントロピー収集追加
  - `src/drivers/ne2000.c` — エントロピー収集追加
  - `src/kernel.c` — 起動時ジッタ収集、prng_init()
  - `src/lib/ssh_crypto.c` — PRNG 統合

## 完了条件

- 起動後 1 秒以内に 256 bit 以上のエントロピーが貯まる
- PRNG が暗号学的シードで初期化される
- SSH の既存機能が壊れない
- BearSSL に渡せるインターフェースが用意されている

## 依存と後続

- 依存: なし（独立して実装可能）
- 後続: Plan 07 (BearSSL 移植で PRNG コールバック使用)

---

## 技術調査結果

### A. Intel 8254 PIT からのエントロピー収集

#### Sodex の PIT 設定 (pit8254.c / pit8254.h)

- `CLOCK_TICK_RATE = 1193180` Hz, `HZ = 100` → 10ms 間隔割り込み
- `LATCH = 11931` (カウンタ初期値)
- I/O ポート: `PIT_COUNTER0 = 0x40`, `PIT_CONTROL = 0x43`
- ラッチコマンド定数: `PITCTL_LATCH = 0x00` が既に定義済み

#### LSB 読み取り方法

```c
/* ラッチコマンドで安全にカウンタを読む */
out8(0x43, 0x00);           /* PITCTL_COUNTER0 | PITCTL_LATCH */
u8 lsb = in8(0x40);        /* 下位バイト */
u8 msb = in8(0x40);        /* 上位バイト */
```

エントロピー収集には LSB のみが必要なので、`PITCTL_RW_L8` (0x10) モードなら `in8(0x40)` 一回で下位8ビットを取得可能。

#### エントロピー推定量

- CPU クロックと PIT クロックは非同期のため、タイマー割り込みハンドラ内で PIT カウンタ LSB を読むとジッタが生じる
- Linux kernel の jitterentropy 実装では、64サンプルを収集し XOR 混合で 64ビットの内部状態に蓄積
- 実機では 1 サンプルあたり約 1–2 ビットのエントロピー。128ビット蓄積には 64–128 回のサンプリング

#### QEMU 環境での品質

- PIT がエミュレートされるため、ジッタの品質は実機より劣る
- ただし VM スケジューリングによる非決定的遅延自体が追加ジッタ源になる
- **推奨**: QEMU テスト時は固定シードのフォールバックを用意し、本番（実機）では PIT ジッタを使う二段構え

### B. AES-CTR DRBG の仕様 (NIST SP 800-90A Rev.1)

#### CTR_DRBG の内部状態 (AES-256 使用時)

| パラメータ | 値 |
|---|---|
| Key (鍵) | 256ビット |
| V (カウンタ) | 128ビット (AES ブロックサイズ) |
| seedlen | 384ビット (Key + V) |
| セキュリティ強度 | 256ビット |

#### 主要アルゴリズム

- **Instantiate**: entropy_input (≥256ビット) + nonce (≥128ビット) → 導出関数 (Block_Cipher_df) で seedlen に圧縮 → Update(seed_material, Key, V)
- **Generate**: V をインクリメントしながら AES-256(Key, V) でブロック生成 → Update → reseed_counter++
- **Reseed**: entropy_input + additional_input → Update → reseed_counter = 1

#### 制約値

| 制約 | 値 |
|---|---|
| 最大リシード間隔 | 2^48 リクエスト |
| 1回あたり最大出力 | 65,536バイト (64KB) |
| 最小エントロピー入力長 | 256ビット (AES-256) |

### C. SHA-256 によるエントロピープールの縮約

#### Sodex の既存実装

`src/lib/sha256.c` に SHA-256 実装が既に存在する。

#### 推奨パターン

```
[エントロピーソース] → XOR混合 → [256ビット以上のプール] → SHA-256 → [256ビットシード]
```

1. **蓄積**: PIT ジッタ、キーボードタイミング、割り込み間隔をプールに XOR 混合
2. **縮約**: プール全体を SHA-256 に入力して 256ビットダイジェスト取得
3. **フィードバック**: ダイジェストの一部をプールに戻して状態更新

256ビット (32バイト) のプールでも十分機能する。エントロピー投入時にプール全体を SHA-256 で再ハッシュすることで、入力品質が低くても出力の統計的品質を保証できる。

#### Linux kernel の参考設計

- 入力プール: 4096ビットのリングバッファ
- 混合: LFSR ベースの twist table で XOR 混合
- 出力: ハッシュ値の半分をプールにフィードバック、残り半分を出力
- Linux 5.17 以降: SHA-1 → BLAKE2s に移行

### 参考資料

- [NIST SP 800-90A Rev.1 (PDF)](https://nvlpubs.nist.gov/nistpubs/specialpublications/nist.sp.800-90ar1.pdf)
- [Jitter Entropy Library - GitHub](https://github.com/smuellerDD/jitterentropy-library)
- [Linux RNG Architecture - Cloudflare Blog](https://blog.cloudflare.com/ensuring-randomness-with-linuxs-random-number-generator/)
- [Programmable Interval Timer - OSDev Wiki](https://wiki.osdev.org/Programmable_Interval_Timer)
