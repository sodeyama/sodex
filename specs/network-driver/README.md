# Network Driver Spec

NE2000 ドライバと uIP TCP/IP スタックを統合し、Sodex で QEMU 上のネットワーク通信を安定運用する。

## 現状

基礎実装はかなり進んでいる。

- `clock_time()` は `kernel_tick` を返す
- PIT 初期化と tick 更新は入っている
- `ne2000_receive()` は実装済み
- NE2000 割り込みハンドラは実装済み
- `network_init()` / `network_poll()` は実装済み
- `tcpip_output()` と `uip_appcall()` は実装済み
- QEMU 上の統合テスト基盤もある

根拠コード:

- `src/net/clock-arch.c`
- `src/pit8254.c`
- `src/drivers/ne2000.c`
- `src/net/netmain.c`
- `src/net/uip-conf.c`
- `src/test/ktest.c`
- `src/test/run_qemu_ktest.py`

## ステータス

| # | ファイル | 状態 | 概要 |
|---|---------|------|------|
| 01 | [01-clock-time.md](plans/01-clock-time.md) | 完了 | PIT 有効化、`clock_time()` 実装 |
| 02 | [02-ne2000-receive.md](plans/02-ne2000-receive.md) | 完了 | NE2000 受信処理 |
| 03 | [03-interrupt-handler.md](plans/03-interrupt-handler.md) | 完了 | NE2000 割り込みハンドラ |
| 04 | [04-kernel-integration.md](plans/04-kernel-integration.md) | 完了 | カーネル初期化への統合 |
| 05 | [05-network-poll.md](plans/05-network-poll.md) | 完了 | ネットワークポーリングループ |
| 06 | [06-tcpip-output.md](plans/06-tcpip-output.md) | 完了 | `tcpip_output()` / `uip_appcall()` |
| 07 | [07-qemu-test.md](plans/07-qemu-test.md) | 完了 | QEMU 統合テストと通常起動 smoke を確認 |
| 08 | [08-ping-validation.md](plans/08-ping-validation.md) | 完了 | `ping` 疎通を `ktest` に組み込み |
| 09 | [09-qemu-hardcode-cleanup.md](plans/09-qemu-hardcode-cleanup.md) | 完了 | QEMU 固定値を driver accessor / config に寄せた |
| 10 | [10-kernel-debug-cleanup.md](plans/10-kernel-debug-cleanup.md) | 完了 | 起動経路の一時デバッグを削除 |
| 11 | [11-network-config-normalization.md](plans/11-network-config-normalization.md) | 完了 | 共有ネットワーク設定へ正規化 |

## 残タスク

`network-driver` spec の範囲では、現時点で残タスクなし。

今回の確認結果:

1. `make test-qemu` で `icmp_ping_gateway` を含む `18/18 passed`
2. 通常カーネルへ戻した後、`make -C src test-qemu-shell-io` が通過

## スコープ外

以下はネットワーク基盤の次段なので、この spec の主対象から外す。

- `accept()` 実装
- SSH server
- HTTP server
- 認証や管理プロトコル

これらは `specs/server-runtime/` で扱う。

## メモ

- 旧 `01-07` は着手前の設計メモを含む
- `08-11` は完了済みの実装メモとして残す
