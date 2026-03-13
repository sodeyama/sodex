# Network Driver Spec

NE2000ドライバとuIP TCP/IPスタックを統合し、Sodexでネットワーク通信を実現する。

## ゴール

QEMUの仮想ネットワーク上でpingが通ること（Phase 1 = ideas/network-llm-api.md の Phase 1）。

## Plans

実装順序に従って番号付け。基本的に上から順に実装する。

| # | ファイル | 概要 | 依存 |
|---|---------|------|------|
| 01 | [01-clock-time.md](plans/01-clock-time.md) | PIT有効化、clock_time()実装 | なし |
| 02 | [02-ne2000-receive.md](plans/02-ne2000-receive.md) | NE2000受信処理（read_remote_dma, リングバッファ） | なし |
| 03 | [03-interrupt-handler.md](plans/03-interrupt-handler.md) | NE2000割り込みハンドラ改修 | なし |
| 04 | [04-kernel-integration.md](plans/04-kernel-integration.md) | カーネル初期化にNE2000/uIP統合 | 03 |
| 05 | [05-network-poll.md](plans/05-network-poll.md) | ネットワークポーリングループ | 01, 02, 03, 04 |
| 06 | [06-tcpip-output.md](plans/06-tcpip-output.md) | tcpip_output()コールバック実装 | 02 |
| 07 | [07-qemu-test.md](plans/07-qemu-test.md) | QEMU上での統合テスト・ping疎通確認 | 01-06全て |

## 変更対象ファイル

### 既存
- `src/pit8254.c` — tickカウンタ追加、PIT有効化
- `src/drivers/ne2000.c` — receive実装、割り込みハンドラ改修
- `src/kernel.c` — NE2000/uIP初期化、ポーリングループ
- `src/net/uip-conf.c` — tcpip_output()実装
- `src/net/clock-arch.c` — clock_time()実装
- `src/include/ne2000.h` — extern宣言追加
- `src/include/clock-arch.h` — CLOCK_CONF_SECOND調整

### 新規
- `src/net/netmain.c` — network_init(), network_poll()
