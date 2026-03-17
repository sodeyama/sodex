# Plan 00: ユーザランド基盤整備

## 概要

Agent Transport の全コンポーネントをユーザ空間プロセスとして構築する方針に基づき、
既存のユーザ空間ライブラリ（libc）に不足している関数の追加と、
カーネル側のソケット層の改修を行う。

## 背景

ユーザ空間には既に充実した基盤がある:
- ソケット syscall ラッパー一式（socket/connect/send/recv/sendto/recvfrom/close）
- 基本文字列関数（strlen, memcpy, strcmp, strncmp, strchr 等）
- printf（ただし `%d` 非対応）、malloc/free、atoi
- ネットワークユーティリティ（htons/ntohs/htonl/ntohl, inet_aton/inet_ntoa）
- debug_write syscall（シリアル出力）、get_kernel_tick syscall（時刻取得）
- sleep_ticks syscall（スリープ）

しかし HTTP/JSON/DNS/TLS/SSE の実装に必要な以下の関数が欠けている。

## 目標

### A. ユーザ空間 libc 拡張

以下の関数をユーザ空間ライブラリに追加する:

| 関数 | 用途 | 必要とする Plan |
|------|------|----------------|
| `printf` の `%d`/`%u` 対応 | 数値デバッグ出力、Content-Length 出力 | 全 Plan |
| `snprintf()` | バッファ安全な文字列フォーマット（HTTPリクエスト生成等） | 02, 03, 05, 10 |
| `strstr()` | `\r\n\r\n` 検索、部分文字列検索 | 02, 09 |
| `strncasecmp()` | HTTP ヘッダの case-insensitive 比較 | 02 |
| `strtol()` | Content-Length, HTTP ステータスコード等の数値パース | 02, 05 |
| `strcat()` / `strncat()` | 文字列連結 | 02, 10 |
| `debug_printf()` | シリアル出力へのフォーマット付きログ（debug_write syscall 使用） | 全 Plan |

### B. カーネル側ソケット改修

ユーザランドのネットワーク処理は、カーネルの `kern_connect()` / `kern_recvfrom()` /
`kern_close_socket()` を syscall 経由で呼ぶ。これらの関数が現在抱える問題を修正する:

| 問題 | 現状 | 改修内容 |
|------|------|----------|
| connect タイムアウト | イテレーション数ベース (20M 回) | PIT tick ベース (10秒) |
| recv タイムアウト | イテレーション数ベース (5M 回) | PIT tick ベース (設定可能) |
| close 待ちタイムアウト | イテレーション数ベース (10M 回) | PIT tick ベース |
| エラーコード | 全て -1 | timeout/refused/ARP失敗を区別 |
| 受信バッファサイズ | 4096 バイト | 8192 バイトに拡張 |
| 送信バッファサイズ | 1460 バイト (MSS) | 分割送信対応 |

### C. ソケットタイムアウト設定 syscall

SSE ストリーミングでは recv タイムアウトを長く（60秒以上）、
通常の HTTP では短く（5秒）設定する必要がある。
`setsockopt` 相当の syscall を追加する:

```c
#define SYS_CALL_SETSOCKOPT  414

/* カーネル側 */
int kern_setsockopt(int sockfd, int level, int optname,
                    const void *optval, int optlen);

/* ユーザ空間 */
int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen);
```

## 設計

### printf %d/%u 実装

```c
/* src/usr/lib/libc/stdio.c に追加 */
case 'd':
{
    int val = va_arg(ap, int);
    char buf[12];
    int i = 0;
    if (val < 0) { putc('-'); val = -val; }
    if (val == 0) { putc('0'); break; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) putc(buf[--i]);
}
break;
```

### snprintf 実装

printf と同じフォーマット処理だが、出力先をバッファにする。
`%s`, `%d`, `%u`, `%x`, `%c`, `%%` をサポート。
バッファオーバーフロー防止のため書き込み位置を常にチェック。

### debug_printf 実装

```c
/* src/usr/lib/libc/debug.c */
void debug_printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    debug_write(buf, len);  /* SYS_CALL_DEBUG_WRITE → シリアル出力 */
}
```

### strstr 実装

```c
char *strstr(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle && strncmp(haystack, needle, nlen) == 0)
            return (char *)haystack;
    }
    return NULL;
}
```

### カーネル側 connect タイムアウト改修

```c
/* socket.c: kern_connect() のポーリングループ */
u_int32_t deadline = kernel_tick + (TCP_CONNECT_TIMEOUT_MS / 10);
while ((int)(kernel_tick - deadline) < 0) {
    disableInterrupt();
    network_poll();
    enableInterrupt();
    if (sk->state == SOCK_STATE_CONNECTED) return 0;
    if (sk->state == SOCK_STATE_CLOSED) return SOCK_ERR_REFUSED;
}
return SOCK_ERR_TIMEOUT;
```

### setsockopt syscall

```c
/* カーネル側: socket.c */
int kern_setsockopt(int sockfd, int level, int optname,
                    const void *optval, int optlen)
{
    struct kern_socket *sk = &socket_table[sockfd];
    if (optname == SO_RCVTIMEO) {
        u_int32_t ms = *(u_int32_t *)optval;
        sk->timeout_ticks = ms / 10;  /* ms → PIT ticks */
        return 0;
    }
    return -1;
}
```

## 実装ステップ

### Phase 00-A: libc 拡張（カーネル変更なし）

1. `printf` に `%d` と `%u` フォーマット指定子を追加する
2. `vsnprintf()` / `snprintf()` を実装する
3. `strstr()` を実装する
4. `strncasecmp()` を実装する
5. `strtol()` を実装する
6. `strcat()` / `strncat()` を実装する
7. `debug_printf()` を実装する（debug_write syscall 使用）

### Phase 00-B: カーネル改修

8. `kern_connect()` のタイムアウトを PIT tick ベースに書き換える
9. `kern_recvfrom()` のタイムアウトを PIT tick ベースに書き換える
10. `kern_close_socket()` のクローズ待ちを PIT tick ベースにする
11. ソケットエラーコードを定義し区別できるようにする
12. `SOCK_RXBUF_SIZE` を 4096 → 8192 に拡張する
13. `kern_send()` / `kern_sendto()` で MSS 超のデータを分割送信する

### Phase 00-C: setsockopt syscall

14. `kern_setsockopt()` をカーネル側に実装する
15. `SYS_CALL_SETSOCKOPT` (414) を `syscalldef.h` と `syscall.c` に登録する
16. ユーザ空間の syscall ラッパー `setsockopt.S` を追加する
17. `sys/socket.h` に `setsockopt()` 宣言と `SO_RCVTIMEO` 定数を追加する

## テスト

### libc 拡張テスト

- ホスト側単体テスト (`tests/test_libc_extensions.c`):
  - `snprintf` のフォーマット各種
  - `strstr` の正常系/異常系
  - `strncasecmp` の大文字小文字混在比較
  - `strtol` の正数/負数/16進数/エラー

### カーネル改修テスト

- QEMU スモーク:
  - connect が 10 秒以内にタイムアウトする（サーバ不在時）
  - connect → send → recv → close のサイクルが 3 回通る
  - エラーコードで timeout と refused を区別できる
  - 4096 バイト超のレスポンスを受信できる

### setsockopt テスト

- QEMU スモーク:
  - setsockopt(SO_RCVTIMEO, 1000) → recv が 1 秒でタイムアウト
  - setsockopt(SO_RCVTIMEO, 60000) → recv が 60 秒待てる

## 変更対象

- ユーザ空間:
  - `src/usr/lib/libc/stdio.c` — printf 拡張、snprintf 追加
  - `src/usr/lib/libc/string.c` — strstr, strncasecmp, strcat, strncat
  - `src/usr/lib/libc/stdlib.c` — strtol
  - `src/usr/lib/libc/debug.c` — debug_printf（新規）
  - `src/usr/include/stdio.h` — snprintf, vsnprintf, debug_printf
  - `src/usr/include/string.h` — strstr, strncasecmp, strcat, strncat
  - `src/usr/include/stdlib.h` — strtol
  - `src/usr/lib/libc/i386/setsockopt.S` — syscall ラッパー（新規）
  - `src/usr/include/sys/socket.h` — setsockopt, SO_RCVTIMEO
- カーネル:
  - `src/socket.c` — タイムアウト改修、エラーコード、バッファ拡張、分割送信
  - `src/include/socket.h` — エラーコード定義、バッファサイズ変更
  - `src/syscall.c` — setsockopt syscall 登録
  - `src/include/sys/syscalldef.h` — SYS_CALL_SETSOCKOPT 追加

## 完了条件

- ユーザ空間から `snprintf`, `strstr`, `strncasecmp`, `strtol`, `debug_printf` が使える
- `printf("%d", 42)` で `42` が出力される
- kern_connect() が 10 秒以内に成功/失敗を返す
- recv タイムアウトが setsockopt で設定可能
- 8KB のレスポンスを受信できる
- connect/close を 3 回繰り返して socket リークしない

## 依存と後続

- 依存: なし（最初に着手する Plan）
- 後続: Plan 01〜11 の全 Plan がこの基盤に依存

---

## 技術調査結果: SOCK_RXBUF_SIZE 拡張と Page Fault

### 現象

`SOCK_RXBUF_SIZE` を 4096 → 8192 に拡張すると、QEMU 上で NE2000 ドライバ初期化後に
Page Fault が発生する (`CR2=C03A5404`)。4096 に戻すと正常動作する。

### 調査結果

#### カーネルメモリレイアウト

| 項目 | RXBUF=4096 | RXBUF=8192 |
|------|-----------|-----------|
| `__bss_start` | `0xC0100000` (物理 1MB) | `0xC0100000` (物理 1MB) |
| `__bss_end` | `0xC05B1DE8` (物理 5.7MB) | `0xC061DBB8` (物理 6.1MB) |
| BSS サイズ | 約 4.9MB | 約 5.3MB |
| `socket_table` | `0x16BC0` (約 93KB) | 約 158KB |

#### 起動時ページテーブル (startup.S)

- `first_pg_tbl1` + `first_pg_tbl2` = 4KB ページ × 1024 × 2 = **8MB マップ**
- BSS は 5.7MB〜6.1MB で、いずれも 8MB 以内 → **初期マップ範囲内**

#### 本ページング (page.c: init_paging)

- PSE（4MB Page Size Extension）で PDE 768〜831 = 最大 256MB マップ
- `init_paging()` は `start_kernel()` 内で `init_mem()` 後に呼ばれる
- 呼ばれた時点で初期 8MB マップ → PSE ベースのフルマップに切り替わる

#### PF の原因: BSS レイアウト変更による NE2000 ドライバへの影響

- `CR2=C03A5404` = `ne2000_rx_pending` (`0xC03A5400`) + 4 バイト
- NE2000 ドライバの BSS 内のアクセスで PF 発生
- **初期マップの範囲外アクセスではない**（3.6MB は 8MB 以内）
- RXBUF 拡張で `socket_table` が +65KB 増 → BSS 全体のレイアウトがシフト
- レイアウト変更により、NE2000 ドライバの変数が異なるページ境界に配置され、
  既存の潜在バグ（配列外アクセス or 未初期化ポインタ）が顕在化した可能性

### 対応方針

PF の根本原因は SOCK_RXBUF_SIZE ではなく、BSS レイアウト依存の NE2000 ドライババグ。
対応は 2 段階:

#### Phase 1: NE2000 ドライバの PF 修正（優先）

1. `ne2000_rx_pending` 周辺のコードを精査
2. PF 発生時の `eip=C001081A` をリンカマップで特定し、問題の命令を確認
3. 配列境界チェック、未初期化ポインタ、アライメントの問題を修正
4. 修正後に RXBUF=8192 で再テスト

#### Phase 2: 初期ページテーブル拡張（将来のための保険）

現在の 8MB マップは BSS 6MB で余裕が 2MB しかない。
Agent Transport のユーザランドコード（BearSSL 等）を追加するとカーネル BSS がさらに増える可能性。

**選択肢 A: startup.S で初期ページテーブルを 3 つに増やす（12MB マップ）**

```asm
# 現在: first_pg_tbl1 (0-4MB) + first_pg_tbl2 (4-8MB)
# 追加: first_pg_tbl3 (8-12MB)

.section ".bss.page_aligned","aw",@nobits
first_pg_tbl3:
  .space 1024*4

# startup.S の初期化ループを 1024*3-1 に変更
# ページディレクトリに 3 エントリ設定
```

- メリット: 単純、確実
- デメリット: BSS に 4KB 追加（無視できるレベル）

**選択肢 B: startup.S で PSE を使い 4MB ページを 2 エントリ設定（8MB マップ、ページテーブル不要）**

```asm
# CR4 で PSE を有効化
movl %cr4, %eax
orl  $0x10, %eax   # PSE bit
movl %eax, %cr4

# ページディレクトリに直接 4MB ページを設定
# PDE[0]   = 0x00000083  (0-4MB, PS=1, RW=1, P=1)
# PDE[1]   = 0x00400083  (4-8MB)
# PDE[768] = 0x00000083  (higher half mirror)
# PDE[769] = 0x00400083
```

- メリット: ページテーブル不要、PDE 追加だけで 4MB 単位で拡張可能
- デメリット: PSE を起動時から使うので page.c との整合性を確認する必要がある

**推奨**: 選択肢 A（ページテーブル追加）が最も安全。PSE は init_paging() で有効化済みなので、
起動時は従来の 4KB ページテーブル方式を維持する方がデバッグしやすい。

### 参考資料

- [Higher Half x86 Bare Bones - OSDev Wiki](https://wiki.osdev.org/Higher_Half_x86_Bare_Bones)
- [Kernel size considerations when initializing paging - OSDev Forum](https://forum.osdev.org/viewtopic.php?t=29134)
- [PSE discussion - OSDev Forum](https://forum.osdev.org/viewtopic.php?f=1&t=29745)
- [Paging - OSDev Wiki](https://wiki.osdev.org/Paging)
