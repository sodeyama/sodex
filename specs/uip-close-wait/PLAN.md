# uIP TCP半閉鎖（CLOSE_WAIT）実装計画

## Issue
GitHub Issue #10: uIP TCP半閉鎖（CLOSE_WAIT）未対応によりcurlで大きいHTTPSレスポンスが途中で切れる

## 問題の本質

uIPのTCPステートマシンはCLOSE_WAIT状態をスキップし、FIN受信時にESTABLISHED → LAST_ACKへ直接遷移する。これによりFIN+ACKを即座に送信してしまい、アプリケーションが残りのデータを読み取る機会を失う。

### RFC 793準拠の正しいフロー
```
サーバー                    Sodex (uIP)
   |--- DATA+FIN ------------>|  (サーバーがレスポンス完了、FIN送信)
   |                          |  → ESTABLISHED → CLOSE_WAIT
   |<-------- ACK -----------|  (ACKのみ送信、FINは送らない)
   |                          |  アプリケーションがバッファのデータを読み取り
   |                          |  アプリケーションがclose()呼び出し
   |<------ FIN+ACK ---------|  → CLOSE_WAIT → LAST_ACK
   |--- ACK ----------------->|  → LAST_ACK → CLOSED
```

### 現在のuIPの誤ったフロー
```
サーバー                    Sodex (uIP)
   |--- DATA+FIN ------------>|
   |                          |  → ESTABLISHED → LAST_ACK (CLOSE_WAITをスキップ!)
   |<------ FIN+ACK ---------|  (即座にFIN+ACK、アプリのclose()を待たない)
   |--- ACK ----------------->|  → LAST_ACK → CLOSED
   |                          |  ソケットがCLOSEDになり、以後のデータ受信不可
```

## 影響範囲

- `src/net/uip.c` — TCPステートマシン本体
- `src/net/uip-conf.c` — uIPアプリケーションコールバック
- `src/socket.c` — kern_recvfrom、socket_begin_close
- `src/include/uip.h` — TCP状態定数
- `src/include/socket.h` — ソケット状態定数

## 設計方針

### 1. uIP層の変更（コア）

**新しい状態定数を追加**:
```c
#define UIP_CLOSE_WAIT  9   // UIP_LAST_ACK(8)の次
```

**FIN受信時の遷移を修正**（uip.c ESTABLISHED状態内）:
- 現在: FIN受信 → `UIP_CLOSE` フラグ + `UIP_LAST_ACK` + FIN+ACK送信
- 修正後: FIN受信 → `UIP_CLOSE` フラグ + `UIP_CLOSE_WAIT` + ACKのみ送信

**CLOSE_WAIT状態の処理を追加**:
- periodic処理でアプリケーションにポーリング（データ送信の機会を与える）
- アプリケーションが`uip_close()`を呼んだら → LAST_ACKへ遷移 + FIN送信
- タイムアウト保護（CLOSE_WAITに長時間留まらないよう）

### 2. ソケット層の変更

**新しいソケット状態を追加**:
```c
#define SOCK_STATE_CLOSE_WAIT  6   // peer closed, app can still read
```

**uip_appcall()の修正**（uip-conf.c）:
- `uip_closed()`受信時: 即座にSOCK_STATE_CLOSEDにせず、SOCK_STATE_CLOSE_WAITに遷移
- CLOSE_WAIT状態ではrxバッファへのデータ追加を継続許可

**kern_recvfromの修正**（socket.c）:
- SOCK_STATE_CLOSE_WAIT時: rxバッファにデータがある限り読み取りを許可
- rxバッファが空になったら0を返す（EOF相当）
- 現在の「20回追加ポーリング」ハックを削除

**socket_begin_closeの修正**（socket.c）:
- CLOSE_WAIT状態からの`close()`呼び出しでFIN送信をトリガー

### 3. 既存の緩和策の整理

以下の緩和策は根本修正後に段階的に除去可能:
- kern_recvfromの`close_polls`（20回追加ポーリング）
- tls_recvのリトライ（50回）
- Connection: close ヘッダー除去

## リスク

1. **FIN再送への対応**: CLOSE_WAIT中にACKが失われた場合、サーバーがFINを再送する。再度CLOSE_WAITに入らず、ACKのみ再送する必要がある
2. **CLOSE_WAITリーク**: アプリケーションがclose()を呼ばない場合、ソケットがCLOSE_WAITに留まり続ける → タイムアウト必須
3. **シーケンス番号**: FINは1つのシーケンス番号を消費する。`uip_add_rcv_nxt(1 + uip_len)`は正しい
4. **既存機能への影響**: Agent Transport（Claude API通信）は小さいレスポンスで正常動作中。回帰テストが必要

## 参考情報

- RFC 793: TCP状態遷移図
- RFC 1122 Section 4.2.2.13: コネクションクローズ要件
- lwIP tcp_in.c: CLOSE_WAITの参考実装
- Cloudflare Blog: TCP half-close violations in the wild
- FreeRTOS Forums: eCLOSE_WAITスタック問題
- Zephyr Issue #46350: FIN/ACKロスト時のクローズ失敗
