# タスクリスト: uIP CLOSE_WAIT実装

## Phase 1: uIP TCPステートマシン修正（コア）

- [ ] **T1.1** `src/include/uip.h` に `UIP_CLOSE_WAIT (9)` 状態定数を追加
- [ ] **T1.2** `src/net/uip.c` ESTABLISHED状態のFIN受信処理を修正
  - LAST_ACKへの直接遷移を削除
  - UIP_CLOSE_WAITへ遷移するよう変更
  - FIN+ACK → ACKのみ送信に変更
- [ ] **T1.3** `src/net/uip.c` にCLOSE_WAIT状態のケースを追加
  - periodic処理でのアプリケーションポーリング
  - アプリケーションからのuip_close()呼び出し時にLAST_ACKへ遷移
  - FIN再送への対応（ACKのみ再送）
- [ ] **T1.4** `src/net/uip.c` periodic timer処理にCLOSE_WAITタイムアウトを追加
  - CLOSE_WAITに留まる最大時間を設定（例: 60秒）
  - タイムアウト時に自動クローズ

## Phase 2: ソケット層修正

- [ ] **T2.1** `src/include/socket.h` に `SOCK_STATE_CLOSE_WAIT (6)` を追加
- [ ] **T2.2** `src/net/uip-conf.c` uip_appcall()のクローズ処理を修正
  - UIP_CLOSE受信時: uIPの状態がCLOSE_WAITならSOCK_STATE_CLOSE_WAITへ遷移
  - CLOSE_WAIT状態でもrxバッファへのデータ書き込みを許可
- [ ] **T2.3** `src/socket.c` kern_recvfromを修正
  - SOCK_STATE_CLOSE_WAIT時: rxバッファにデータがあれば読み取り許可
  - rxバッファが空でCLOSE_WAITなら0を返す（EOF）
  - `close_polls`ハックの除去
- [ ] **T2.4** `src/socket.c` socket_begin_close / kern_close_socketの修正
  - CLOSE_WAIT状態からのclose()でuIPにFIN送信を指示
  - close_pending設定後、uip_appcallでuip_close()呼び出し

## Phase 3: テスト・検証

- [ ] **T3.1** ビルド確認（`make clean && make`）
- [ ] **T3.2** QEMU起動確認（カーネルパニックなし）
- [ ] **T3.3** 小さいHTTPSレスポンスのテスト
  - `curl https://httpbin.org/get` — 引き続き完全取得できること
- [ ] **T3.4** 大きいHTTPSレスポンスのテスト
  - `curl https://www.yahoo.co.jp` — 全レスポンス取得できること
- [ ] **T3.5** Agent Transport通信テスト
  - Claude APIへの通信が引き続き正常に動作すること
- [ ] **T3.6** TCP接続の正常クローズ確認
  - ソケットリーク（CLOSE_WAITに留まり続ける）がないこと

## Phase 4: クリーンアップ

- [ ] **T4.1** 緩和策の除去検討
  - kern_recvfromの`close_polls`（20回追加ポーリング）
  - tls_recvの50回リトライ
  - Connection: closeヘッダー除去ロジック
- [ ] **T4.2** uip.cのコメント更新
  - 「CLOSE_WAIT is not implemented」コメント（line 1463-1466）を更新
- [ ] **T4.3** Issue #10をクローズ

## 依存関係

```
T1.1 → T1.2, T1.3, T1.4 (状態定数が先)
T2.1 → T2.2, T2.3, T2.4 (ソケット状態定数が先)
T1.2, T1.3 → T2.2 (uIP側の変更が先)
Phase 1, 2 → Phase 3 (実装完了後にテスト)
Phase 3 → Phase 4 (テスト通過後にクリーンアップ)
```

## 優先度

- Phase 1, 2: **高** — 根本原因の修正
- Phase 3: **高** — 回帰確認
- Phase 4: **中** — 根本修正が安定した後でよい
