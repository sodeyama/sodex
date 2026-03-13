# Plan 1.6: tcpip_output() の実装

## 概要

uIPがパケット送信時に呼ぶコールバック `tcpip_output()` を実装し、
uIPスタックからNE2000ドライバへの送信パスを接続する。

## 現状

### tcpip_output()（src/net/uip-conf.c）
```c
void tcpip_output(void)
{
    // 空関数
}
```

### uip_appcall()（src/net/uip-conf.c）
```c
void uip_appcall(void)
{
    // 空関数
}
```

### 呼ばれるタイミング

`tcpip_output()` はuIPスタック内部から、送信すべきパケットが
`uip_buf` に準備されたときに呼ばれる。

`uip_appcall()` はTCP接続に対するイベント（接続、受信、切断等）が
発生したときにuIPから呼ばれるアプリケーションコールバック。

## 実装

### tcpip_output()

**ファイル**: `src/net/uip-conf.c`

```c
#include <uip.h>
#include <uip_arp.h>
#include <ne2000.h>

void tcpip_output(void)
{
    if (uip_len > 0) {
        uip_arp_out();
        ne2000_send(uip_buf, uip_len);
    }
}
```

処理内容:
1. `uip_arp_out()`: IPパケットにEthernetヘッダを付与（ARPテーブルからMAC解決）
2. `ne2000_send()`: Ethernetフレームをネットワークに送信

### uip_appcall()

Phase 1の段階では、TCPアプリケーションはまだないので空のままでよい。
Phase 2でechoクライアント等を実装するときに拡張する。

```c
void uip_appcall(void)
{
    // Phase 2 で実装
}
```

## network_poll() との関係

`tcpip_output()` と `network_poll()` 内の送信処理は役割が異なる:

- **network_poll() 内の送信**: `uip_input()` や `uip_periodic()` の直後に
  `uip_len > 0` をチェックして送信。これがメインの送信パス。
- **tcpip_output()**: uIPが内部的にパケット送信が必要なときのコールバック。
  主に `uip_connect()` 等のAPI呼び出し時に使われる。

両方実装しておくことで、どちらの経路からでも送信が行われる。

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `src/net/uip-conf.c` | `tcpip_output()` 実装、ヘッダインクルード追加 |

## 依存関係

- Plan 1.2（ne2000_receive）: ne2000_send() は既に実装済みなので直接の依存なし
- Plan 1.5（network_poll）: 送信パスの補完

## 完了条件

- [ ] `tcpip_output()` が `uip_arp_out()` + `ne2000_send()` を呼ぶ
- [ ] uIPが接続要求等で内部的にパケット送信するときに正しく送信される
