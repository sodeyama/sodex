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
| 07 | [07-qemu-test.md](plans/07-qemu-test.md) | 一部完了 | QEMU 統合テストはあるが、`ping` 完了条件は未クローズ |
| 08 | [08-ping-validation.md](plans/08-ping-validation.md) | 未着手 | `ping` 疎通の完了条件を閉じる |
| 09 | [09-qemu-hardcode-cleanup.md](plans/09-qemu-hardcode-cleanup.md) | 未着手 | `0xC100` など QEMU 固定前提の整理 |
| 10 | [10-kernel-debug-cleanup.md](plans/10-kernel-debug-cleanup.md) | 未着手 | 起動経路のデバッグコード分離 |
| 11 | [11-network-config-normalization.md](plans/11-network-config-normalization.md) | 未着手 | IP / gateway / test 設定の正規化 |

## 残タスク

今の残タスクは次の 4 つに絞られる。

1. `ping` 疎通の完了条件を、自動または再現可能な手順で閉じる
2. NIC I/O ベースや QEMU 前提値の直書きをドライバ外へ寄せる
3. `kernel.c` に残っている bring-up 用デバッグコードを整理する
4. 通常起動と ktest のネットワーク設定を揃える

## スコープ外

以下はネットワーク基盤の次段なので、この spec の主対象から外す。

- `accept()` 実装
- SSH server
- HTTP server
- 認証や管理プロトコル

これらは `specs/server-runtime/` で扱う。

## メモ

- 旧 `01-07` は着手前の設計メモとして残す
- 現状の実装を前提に、今後は `08-11` を優先する
