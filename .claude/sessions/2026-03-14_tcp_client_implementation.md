# TCP/IPクライアントサイド実装 — 2026-03-14

## 目標
TCP/IPをクライアントサイドで使えるようにする（connect → send → recv）

## 成果

### TCP接続・送信・受信すべてQEMU上で動作確認済み

```
[PASS] arp_resolution
[PASS] socket_create_tcp
[PASS] socket_create_udp
[PASS] tcp_connect
[PASS] tcp_send
TCP: NEWDATA len=6        ← echoサーバから6バイト受信
[TCP] Received 6 bytes: HELLO\x0A
```

echoサーバ側でも `Accepted connection` を確認。

## 修正した主要バグ

### 1. TCP send: uip_send()をappcallコンテキスト外で呼んでいた (socket.c)
- **問題**: `kern_sendto` で直接 `uip_send(buf, len)` を呼んでいたが、uIPでは `uip_send()` は appcall コールバック内でのみ有効
- **修正**: socket構造体にTXバッファ (`tx_buf[1460]`, `tx_len`, `tx_pending`) を追加。`kern_sendto` はバッファにコピーして `uip_poll_conn()` でappcallをトリガー。appcall内のpoll/acked/connectedイベントでTXバッファから `uip_send()` を呼ぶ

### 2. TCP connect: SYNがARP requestに置き換えられていた (socket.c)
- **問題**: `uip_connect()` 後に `uip_poll_conn()` でSYN送信を試みていたが、`UIP_POLL_REQUEST` は `UIP_ESTABLISHED` 状態でしかappcallを呼ばない（SYN_SENT状態ではdrop）。さらに `uip_periodic_conn()` でSYNを生成しても、宛先MACアドレスがARP tableにない場合、`uip_arp_out()` がSYNパケットをARP requestで上書きする
- **修正**: 初回の `uip_periodic_conn()` でARP requestが送信される → ポーリングループ内で500K回ごとにSYN再送を試みる → ARP解決後の再送でSYNが実際に送信される → SYN-ACK受信でCONNECTED状態に遷移

### 3. TCP connect: sleep_onがプロセスコンテキスト外で使えない (socket.c)
- **問題**: `kern_connect` が `sleep_on(&sk->connect_wq)` でブロックしていたが、カーネルテスト環境ではプロセススケジューラが起動していない
- **修正**: ポーリングベースに変更。`network_poll()` を繰り返し呼んでSYN-ACKの到着を待つ

### 4. network_poll: ne2000_rx_pendingフラグに依存しすぎ (netmain.c)
- **問題**: パケット受信をIRQハンドラによる `ne2000_rx_pending` フラグにのみ依存していたが、割り込みタイミングやISRクリアの問題で見落とすことがあった
- **修正**: ISRレジスタを直接チェック。さらに毎回 `ne2000_receive()` を試みる方式に変更

### 5. uip_appcall: TCP retransmit未対応 (uip-conf.c)
- **問題**: `uip_rexmit()` イベントで何もしていなかった
- **修正**: TXバッファの内容を再送（`uip_send(sk->tx_buf, sk->tx_len)`）

## 変更ファイル一覧

| ファイル | 変更内容 |
|----------|----------|
| `src/include/socket.h` | `SOCK_TXBUF_SIZE`, `tx_buf[]`, `tx_len`, `tx_pending` 追加 |
| `src/socket.c` | TCP send: TXバッファ経由 + uip_poll_conn。TCP connect: ポーリングベース + SYN再送 |
| `src/net/uip-conf.c` | appcall: poll/acked/connected時にTXバッファ送信、rexmit対応、デバッグ出力 |
| `src/net/netmain.c` | ISRダイレクトチェック、always-receive方式、デバッグ出力 |
| `src/drivers/ne2000.c` | 空バッファ時のデバッグ出力抑制 |
| `src/test/ktest.c` | ネットワーク初期化 + ARP/socket/TCP/UDPテスト追加 |
| `src/test/echo_server.py` | QEMU guestfwd用TCPエコーサーバ（Python） |
| `src/makefile` | test-qemuターゲット: NE2000 NIC + guestfwd + echoサーバ |

## テストインフラ

### QEMUテスト構成
```
make test-qemu
```
- echoサーバ(Python)をホスト側 127.0.0.1:17777 で起動
- QEMUの `guestfwd=tcp:10.0.2.100:7777-tcp:127.0.0.1:17777` でゲスト→ホスト転送
- NE2000 NIC: `ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56`
- カーネルは `KTEST_BUILD` フラグ付きでビルド → `run_kernel_tests()` が実行される
- シリアルログ: `build/log/test_serial.log`

### テスト項目（15テスト）
1. CPU/descriptor: gdt, idt, paging, interrupts, higher_half (5)
2. Memory: kalloc_kfree, aalloc, many_allocs (3)
3. Network: arp_resolution, socket_create_tcp, socket_create_udp (3)
4. TCP: tcp_connect, tcp_send, tcp_recv (3)
5. UDP: udp_sendto, udp_recvfrom (1)

## 残りの課題

### 高優先度
- [ ] tcp_recvテスト結果がQEMUタイムアウト前に出力されない（タイミング）→ テスト時間を30秒に延長済みだが未確認
- [ ] デバッグログのクリーンアップ（ne2000.c, netmain.c, uip-conf.c, kernel.cのデバッグコード）

### 中優先度
- [ ] kern_connect のsleep_on方式との共存（プロセスコンテキストからの呼び出し時）
- [ ] kern_close_socket のTCP FIN送信（現在uip_close()がappcallコンテキスト外で呼ばれている）
- [ ] 複数同時TCP接続のテスト
- [ ] UDP recvテスト（echoサーバがTCPのみ）

### 低優先度
- [ ] kern_accept (サーバサイド)
- [ ] 非ブロッキングソケット
- [ ] select/poll相当の機能
- [ ] SO_TIMEOUT等のソケットオプション
- [ ] TCP keepalive

## uIPのTCPデータフロー（理解メモ）

```
[送信]
kern_sendto() → tx_buf にコピー、tx_pending=1
  → uip_poll_conn(conn) → uip_process(UIP_POLL_REQUEST)
    → UIP_ESTABLISHED && !outstanding → UIP_APPCALL()
      → uip_appcall() → tx_pending==1 → uip_send(tx_buf, tx_len)
    → appsend → TCPパケット生成
  → uip_arp_out() → イーサネットヘッダ追加
  → ne2000_send()

[受信]
network_poll() → ne2000_receive() → uip_buf に格納
  → uip_input() → uip_process(UIP_DATA)
    → UIP_APPCALL() with UIP_NEWDATA
      → socket_tcp_input() → rxbuf にコピー
  → kern_recvfrom() → rxbuf_read()

[接続確立]
uip_connect() → SYN_SENT, timer=1, len=1
  → uip_periodic_conn() → timer-- → 0 → SYN retransmit
    → uip_arp_out() → MAC未知ならARP requestに置換
  → [ポーリングで定期的にSYN再送]
  → ARP reply受信 → ARP table更新
  → 次のSYN再送 → 実際にSYN送信
  → SYN-ACK受信 → uip_appcall(UIP_CONNECTED)
```
