# EPIC 03: DNS/TLS/BearSSL

## 目的

固定IPと平文HTTPの段階を抜けた後、HTTPS 通信に必要な DNS、エントロピー、BearSSL、
証明書検証を最小構成で成立させる。

## スコープ

- UDP ベース DNS query
- PRNG / エントロピー源
- `memmove()` 等の BearSSL 依存プリミティブ
- BearSSL の I/O コールバック統合
- 証明書検証または証明書ピンニング

## 実装ステップ

1. PIT ジッタなどを使った初期エントロピー源を定義する
2. BearSSL が必要とする libc 互換関数を洗い出して補う
3. `uIP TCP` を BearSSL の read/write コールバックにつなぐ
4. DNS クエリとレスポンス解析を実装し、A レコード解決を通す
5. サーバ証明書の検証方針を決める
   - 初期段階: ピンニングまたは限定的 CA バンドル
   - 後段階: 更新可能な trust store
6. TLS ハンドシェイク失敗の理由を診断ログへ落とす

## 変更対象

- 新規候補
  - `src/net/dns.c`
  - `src/include/dns.h`
  - `src/net/tls_client.c`
  - `src/include/tls_client.h`
  - `src/lib/memmove.c`
  - `tests/test_dns_parser.c`
  - `tests/test_tls_adapter.c`
- 既存
  - `src/socket.c`
  - `src/lib/string.c`
  - `tests/Makefile`

## テスト

- host 単体
  - DNS レスポンス parser
  - TLS adapter の境界条件
- モック結合
  - 自前 TLS サーバ
  - 証明書 mismatch
  - DNS timeout / NXDOMAIN
- QEMU スモーク
  - `HTTPS GET` が 1 回通る
  - 失敗時に handshake failure を識別できる

## 完了条件

- ホスト名から IP を解決できる
- BearSSL 上で HTTPS 応答を 1 本受け取れる
- 証明書不一致時に失敗できる
- TLS 経路が Claude adapter 非依存で再利用可能になっている

## 依存と後続

- 依存: EPIC-01, EPIC-02
- 後続: EPIC-04, EPIC-05, EPIC-09

