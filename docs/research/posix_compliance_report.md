# Sodex POSIX準拠状況レポート

**調査日**: 2026-03-14
**対象**: Sodex OS Kernel (i486, 32-bit)
**目的**: GNU等の外部ライブラリを動作させるためのPOSIX準拠状況の精査

---

## 1. エグゼクティブサマリー

Sodexは基本的なファイルI/O・プロセス管理・シグナル・ソケットを独自実装しているが、**POSIX準拠レベルは非常に低い**。GNUライブラリ（特にglibc/newlib/musl）を動作させるには、以下の主要領域で大幅な拡張が必要である。

### 準拠度スコアカード

| カテゴリ | 現状 | POSIX要件との差分 |
|---------|------|------------------|
| システムコール | 17/~80 実装済 | **大幅に不足** |
| プロセス管理 | fork未実装（スタブ） | **致命的** |
| ファイルシステム | 基本I/Oのみ | **stat/fstat/unlink等なし** |
| シグナル | 基本のみ | **sigaction/sigprocmask等なし** |
| メモリ管理 | brk/sbrkのみ | **mmap/munmap未実装** |
| ユーザー/グループ | 未実装 | **全面的に未実装** |
| 端末制御 | 未実装 | **ioctl/termios未実装** |
| パイプ/IPC | 未実装 | **全面的に未実装** |
| 時刻管理 | 未実装 | **time/gettimeofday未実装** |

---

## 2. 現状の詳細分析

### 2.1 システムコールインターフェース

**実装方式**: `int 0x80` (Linux互換ABI)
- EAX = システムコール番号, EBX/ECX/EDX/ESI/EDI = 引数
- ハンドラ: `src/syscall.c:i80h_syscall()`

**実装済みシステムコール** (17個):

| # | 名称 | 状態 | 備考 |
|---|------|------|------|
| 1 | exit | **実装済** | プロセス終了 |
| 2 | fork | **スタブ** | 常に0を返す。COW/ページコピー未実装 |
| 3 | read | **実装済** | stdin/ext3ファイル対応 |
| 4 | write | **実装済** | stdout/stderr/ext3ファイル対応。**戻り値がvoid（POSIX違反）** |
| 5 | open | **実装済** | ext3ファイルシステム |
| 6 | close | **実装済** | |
| 7 | waitpid | **実装済** | |
| 11 | execve | **実装済** | ELFローダ対応 |
| 12 | chdir | **実装済** | |
| 19 | lseek | **部分実装** | カーネル内関数として存在 |
| 37 | kill | **実装済** | |
| 39 | mkdir | **部分実装** | syscall dispatcherに未登録 |
| 45 | brk | **実装済** | |
| 48 | signal | **実装済** | 旧式signal()のみ |

**独自拡張システムコール**:
- 100: getdentry (ディレクトリエントリ取得)
- 101: getpstat (プロセス構造体取得)
- 250: timer (タイマー)
- 300: memdump (デバッグ用メモリダンプ)
- 400: send (ネットワーク送信)
- 401-410: ソケット操作 (socket/bind/listen/accept/connect/send/recv/sendto/recvfrom/close)

**定義のみで未実装のシステムコール** (syscalldef.hに番号定義あり):

| # | 名称 | GNU libc必要度 |
|---|------|---------------|
| 8 | creat | 中 |
| 9 | link | 中 |
| 10 | unlink | **高** |
| 13 | time | **高** |
| 14 | mknod | 低 |
| 15 | chmod | 中 |
| 18 | stat | **高** |
| 20 | getpid | **高** |
| 21 | mount | 低 |
| 25 | stime | 低 |
| 26 | ptrace | 低 |
| 27 | alarm | 中 |
| 28 | fstat | **高** |
| 29 | pause | 中 |
| 30 | utime | 低 |
| 33 | access | 中 |
| 36 | sync | 低 |
| 38 | rename | 中 |
| 40 | rmdir | 中 |
| 41 | dup | **高** |
| 42 | pipe | **高** |
| 43 | times | 中 |

### 2.2 プロセス管理

**ファイル**: `src/process.c`, `src/execve.c`

**現状**:
- `MAXPROCESS = 3` — 同時実行プロセス数がハードコードで3
- プロセス状態: RUNNING/INTERRUPTIBLE/UNINTERRUPTIBLE/ZOMBIE/STOPPED
- タスク構造体にpid, filename, parent, context, files等のフィールドあり
- `schedule()` によるプリエンプティブスケジューリング（PIT割り込みベース、HZ=100）
- `sleep_on()` / `wakeup()` によるブロッキング機構

**致命的な欠落**:
- **fork()が未実装**: スタブが0を返すだけ。アドレス空間のコピー、ページテーブル複製、COW機構がすべて必要
- **getpid()未実装**: 極めて基本的なPOSIXインターフェース
- **プロセスグループ/セッション未実装**: setpgrp, setsid, getpgrp等
- **wait()未実装**: waitpidのみ
- **_exit()とexit()の区別なし**

### 2.3 ファイルシステム

**ファイル**: `src/ext3fs.c`, `src/fs.c`

**現状**:
- ext3ファイルシステムの読み書き対応
- ファイルディスクリプタテーブル: `FILEDESC_MAX = 32`（POSIX最低要件の`OPEN_MAX >= 20`は満たす）
- O_RDONLY/O_WRONLY/O_RDWR/O_CREAT/O_TRUNC/O_APPEND等のフラグ定義あり
- SEEK_SET/SEEK_CUR/SEEK_END定義あり
- `struct file` にf_dentry, f_mode, f_flags, f_pos等

**欠落**:
- **stat/fstat/lstat未実装**: ファイルメタデータ取得不可。`struct stat`が未定義
- **unlink/rmdir未実装**: ファイル/ディレクトリ削除不可
- **rename未実装**
- **link/symlink未実装**
- **dup/dup2未実装**: ファイルディスクリプタ複製不可（シェルのリダイレクションに必須）
- **pipe未実装**: プロセス間パイプ不可
- **fcntl未実装**: ファイルディスクリプタ制御不可
- **ioctl未実装**: デバイス制御不可
- **chmod/chown/chgrp未実装**: パーミッション管理不可
- **opendir/readdir/closedir未実装**: ディレクトリ走査（独自のgetdentryのみ）
- **sys_write()の戻り値がvoid**: POSIXでは書き込みバイト数を返す必要がある

### 2.4 メモリ管理

**ファイル**: `src/memory.c`, `src/brk.c`

**現状**:
- カーネルヒープ: `kalloc()`/`kfree()` — ファーストフィット方式
- プロセスヒープ: `sys_brk()` 実装済
- ユーザー空間: `malloc()`/`free()` — sbrk()ベース
- ページング基盤: 初期8MB分の4MBページテーブル（`startup.S`）

**欠落**:
- **mmap/munmap未実装**: 多くのCライブラリ（特にglibc）はmmapベースのメモリ割り当てを多用
- **mprotect未実装**: ページ保護属性変更不可
- **動的ページテーブル拡張未実装**: 固定8MBマッピングのみ
- **プロセスごとのアドレス空間分離が不完全**: fork時のページテーブルコピー/COW未対応
- **共有メモリ (shmget/shmat等) 未実装**

### 2.5 シグナル

**ファイル**: `src/signal.c`, `src/include/signal.h`

**現状**:
- 22シグナル定義 (SIGHUP〜SIGTTOU)
- `sys_signal()`: 旧式signal()ハンドラ設定
- `sys_kill()`: シグナル送信
- `sigaction`構造体: sa_handler + sa_maskのみ
- デフォルトハンドラ: signal_dummy, core_dump, task_stop

**欠落**:
- **sigaction()システムコール未実装**: POSIX標準のシグナル設定関数
- **sigprocmask()未実装**: シグナルマスク操作不可
- **sigpending()未実装**
- **sigsuspend()未実装**
- **sigqueue()未実装**: リアルタイムシグナル非対応
- **シグナルキューイング未実装**: ペンディングシグナルはビットマスク（`task_struct.signal`がu_int32_t）
- **SA_RESTARTフラグ未対応**: 割り込まれたシステムコールの自動再開不可

### 2.6 ユーザー空間libc

**ファイル**: `src/usr/lib/libc/`, `src/usr/include/`

**現状のヘッダ一覧**:
```
stdio.h    - printf, putc, puts
stdlib.h   - exit, execve, fork, waitpid, kill, signal, atoi
string.h   - strlen, strcmp, strncmp, strcpy, strncpy, strchr, strrchr, memset, memcpy
stdarg.h   - va_list, va_start, va_end (コンパイラビルトイン利用)
malloc.h   - malloc, free
brk.h      - brk
sbrk.h     - sbrk
math.h     - pow
regex.h    - regex
signal.h   - シグナル番号定義
sys/types.h - int8_t, u_int32_t, pid_t, off_t, size_t, mode_t
sys/socket.h - ソケットインターフェース
netinet/in.h - IPPROTO_*, sockaddr_in
arpa/inet.h  - ネットワークバイトオーダー
```

**欠落している主要ヘッダ（POSIX必須）**:
```
unistd.h     - POSIXシステムコールラッパー全般
fcntl.h      - ファイル制御
sys/stat.h   - ファイルステータス
sys/wait.h   - プロセス待機
dirent.h     - ディレクトリ操作
errno.h      - エラー番号（**現在エラー処理機構なし**）
time.h       - 時刻関数
sys/time.h   - タイムバル
setjmp.h     - 非局所ジャンプ
termios.h    - 端末制御
sys/ioctl.h  - デバイス制御
sys/mman.h   - メモリマッピング
pwd.h        - パスワードデータベース
grp.h        - グループデータベース
limits.h     - 実装制限値
float.h      - 浮動小数点制限値
locale.h     - ロケール
ctype.h      - 文字分類
assert.h     - アサーション
```

### 2.7 ネットワーク

**現状**: uIPベースのTCP/IPスタック、NE2000ドライバ、ソケットAPI (socket/bind/listen/accept/connect/send/recv/sendto/recvfrom) が実装済。

**評価**: ネットワーク部分は比較的良好。POSIX socketsのサブセットをカバー。

### 2.8 端末/TTY

**現状**: VGA直接出力 + キーボード入力のみ。TTYレイヤー未実装。

**欠落**:
- termios構造体/インターフェース全体
- ioctl
- 擬似端末（pty）
- 行規律（line discipline）
- 端末ジョブ制御

### 2.9 時刻管理

**現状**: PIT（8254タイマー）でカーネルtickのみ。`kernel_tick`変数がHZ=100で更新。

**欠落**:
- time() / gettimeofday() / clock_gettime()
- RTC（リアルタイムクロック）ドライバ
- alarm() / setitimer()
- nanosleep() / usleep()

---

## 3. GNUライブラリを動作させるための戦略

### 3.1 ターゲットCライブラリの選定

| ライブラリ | 特徴 | Sodexへの適合性 |
|-----------|------|----------------|
| **glibc** | フル機能、巨大 | **非推奨** — 依存するカーネル機能が多すぎる |
| **musl** | 軽量、POSIX準拠、静的リンク向き | **推奨** — 最小限のsyscallで動作可能 |
| **newlib** | 組み込み向け、カスタマイズ容易 | **代替案** — syscallスタブ層が明確で移植しやすい |
| **dietlibc** | 超軽量 | **検討可** — 機能は限定的 |

**推奨**: **musl libc** をターゲットとする。理由:
1. 必要syscall数が比較的少ない（~40で基本動作可能）
2. 静的リンク前提の設計
3. POSIX.1-2008準拠
4. コードベースが小さく（10万行未満）、移植時のデバッグが容易

### 3.2 実装優先度（フェーズ計画）

#### Phase 1: 最小POSIX基盤 (Critical Path)

**目的**: musl libcの最小ビルドが通り、"Hello World"が動作する

1. **errno機構の導入**
   - グローバル`errno`変数（最初はスレッドローカル不要）
   - 主要エラー番号定義（EINVAL, ENOMEM, ENOENT, EBADF, EACCES等）

2. **sys_write()の戻り値修正**
   - 現在`void`→`ssize_t`に変更（書き込みバイト数を返す）

3. **getpid()実装**
   - `current->pid`を返すだけ

4. **stat/fstat実装**
   - `struct stat`定義
   - ext3 inodeからstat情報へのマッピング

5. **dup/dup2実装**
   - ファイルディスクリプタテーブルの複製

6. **unlink実装**
   - ext3でのinode/ディレクトリエントリ削除

7. **基本ヘッダ整備**
   - unistd.h, fcntl.h, sys/stat.h, errno.h, limits.h, ctype.h

#### Phase 2: プロセス管理 (fork/exec)

**目的**: シェルスクリプトやパイプラインの基本動作

1. **fork()の完全実装**
   - プロセスアドレス空間のコピー（最初は全コピー、後でCOW化）
   - ページテーブル複製
   - ファイルディスクリプタテーブル複製
   - シグナルハンドラ継承

2. **pipe()実装**
   - カーネル内リングバッファ
   - 読み書き側のファイルディスクリプタ

3. **wait()/WEXITSTATUS等マクロ**

4. **プロセス数上限の引き上げ**
   - `MAXPROCESS = 3` → 最低16以上

#### Phase 3: メモリ管理拡張

**目的**: mmapベースのメモリ割り当てを可能にする

1. **mmap/munmap実装**
   - 匿名マッピング（MAP_ANONYMOUS）優先
   - ファイルマッピングは後回し可

2. **動的ページテーブル拡張**
   - 8MB制限の撤廃
   - ページフォルトハンドラでのデマンドページング

3. **mprotect実装**

#### Phase 4: シグナルの完全化

1. **sigaction()システムコール**
2. **sigprocmask()**
3. **SA_RESTARTサポート**
4. **適切なシグナルキューイング**

#### Phase 5: 端末/時刻

1. **ioctl()基盤**
2. **基本termios**（少なくともTCSETATTR/TCGETATTR）
3. **gettimeofday() / clock_gettime()**
4. **RTCドライバ**

#### Phase 6: ファイルシステム拡充

1. **readdir/getdents**
2. **rename, link, symlink**
3. **chmod, chown**
4. **access()**
5. **fcntl()**

---

## 4. musl libc移植時に必要な最小システムコール一覧

musl libcが最低限必要とするシステムコール（Linux番号ベース）:

```
# 現在実装済み（○）/ 未実装（×）

○  1  exit
×  2  fork          ← スタブのみ
○  3  read
○  4  write         ← 戻り値修正必要
○  5  open
○  6  close
○  7  waitpid
○ 11  execve
○ 12  chdir
× 13  time
× 18  stat
× 20  getpid        ← 容易
× 28  fstat         ← 必須
× 33  access
○ 37  kill
× 38  rename
○ 39  mkdir
× 40  rmdir
× 41  dup           ← 必須
× 42  pipe          ← 重要
○ 45  brk
○ 48  signal
× 54  ioctl         ← 必須（端末）
× 63  dup2          ← 必須
× 90  mmap          ← 非常に重要
× 91  munmap
×102  socketcall    ← 個別syscall (401-410) で代替可
×114  wait4
×120  clone         ← fork代替として検討
×122  uname         ← 容易
×125  mprotect
×162  nanosleep
×174  rt_sigaction  ← 重要
×175  rt_sigprocmask
×192  mmap2         ← mmap代替
×195  stat64
×197  fstat64
×199  getuid32
×200  getgid32
×201  geteuid32
×202  getegid32
×220  getdents64    ← ディレクトリ読み取り
×221  fcntl64
×243  set_thread_area ← TLS用（将来）
×252  exit_group
×258  set_tid_address ← スレッド用（将来）
×265  clock_gettime
×295  openat        ← 最新POSIX
×296  mkdirat
×302  renameat
×305  readlinkat
×307  faccessat
×311  set_robust_list
×355  getrandom
×374  userfaultfd
×439  faccessat2
```

---

## 5. 技術的課題と注意点

### 5.1 アドレス空間レイアウト

現在のレイアウト:
```
0x00000000 - 0xBFFFFFFF : ユーザー空間 (3GB)
0xC0000000 - 0xFFFFFFFF : カーネル空間 (1GB, Higher-Half)
```
これはLinux 32-bitと同一レイアウトで、musl/glibc移植に有利。

### 5.2 ELFローダ

`src/elfloader.c`が存在し、ELFバイナリの実行が可能。ただし:
- 動的リンク非対応（.soファイル非対応）
- PIE（位置独立実行形式）非対応の可能性
- → 静的リンクで対応可能

### 5.3 syscall番号の互換性

現在のsyscall番号はLinux i386 ABIと互換（1=exit, 3=read, 4=write等）。ソケット系のみ独自番号（401-410）。musl移植時にはsocketcall(102)またはLinux互換の個別番号に合わせる必要がある。

### 5.4 TLS（Thread Local Storage）

musl libcはerrnoにTLSを使用（`set_thread_area`または`clone`でのTLSセグメント設定）。最初は単純なグローバル変数で回避可能だが、将来的にはGDTにTLSセグメントを追加する必要がある。

---

## 6. 工数見積もり（概算）

| フェーズ | 内容 | 規模感 |
|---------|------|--------|
| Phase 1 | 最小POSIX基盤 | 中規模（errno, stat, dup, unlink, ヘッダ整備） |
| Phase 2 | fork/pipe | 大規模（ページテーブル複製が最大の山） |
| Phase 3 | mmap | 大規模（仮想メモリ管理の大幅拡張） |
| Phase 4 | シグナル完全化 | 中規模 |
| Phase 5 | 端末/時刻 | 中規模 |
| Phase 6 | FS拡充 | 中規模 |

**最大の技術的リスク**: fork()とmmap()の実装。いずれもページテーブル管理の根本的な拡張が必要。

---

## 7. 推奨アプローチ

1. **newlibから始める案も検討**: newlibはsyscallスタブ層が明確で、`_sbrk`, `_write`, `_read`等の最小インターフェースさえ実装すれば動作する。musl移植のステップとして先にnewlibで経験を積むのも有効。

2. **段階的テスト戦略**: 各Phase完了後にQEMU上でテストスイートを実行し、回帰を防ぐ。

3. **Linux syscall互換レイヤーの構築**: syscall番号をLinux i386と完全互換にすることで、既存のmusl/newlibビルドをそのまま利用可能にする。

4. **最初のマイルストーン**: `printf("Hello, World\n")` がmusl/newlibリンクバイナリで動作すること。これにはerrno, write(戻り値修正), brk, exit, fstatの最小実装があれば到達可能。

---

## 付録A: 現在のソースツリー構成（関連部分）

```
src/
├── syscall.c          # システムコールディスパッチャ
├── sys_core.S         # syscall ASMハンドラ, memset/memcpy
├── process.c          # プロセス管理・スケジューラ
├── execve.c           # execve実装
├── signal.c           # シグナル処理
├── ext3fs.c           # ext3ファイルシステム
├── fs.c               # ファイル操作
├── memory.c           # カーネルメモリアロケータ
├── brk.c              # brk syscall
├── socket.c           # ソケットAPI
├── elfloader.c        # ELFローダ
├── include/
│   ├── sys/syscalldef.h  # syscall番号定義
│   ├── process.h         # プロセス構造体
│   ├── signal.h          # シグナル定義
│   ├── fs.h              # ファイルシステム構造体
│   ├── memory.h          # メモリ管理
│   └── sodex/const.h     # 定数・マクロ
├── lib/               # カーネルライブラリ
│   ├── string.c       # strlen, strcmp, strcpy等
│   ├── memb.c         # メモリブロックアロケータ
│   └── delay.c        # 遅延関数
├── usr/
│   ├── include/       # ユーザー空間ヘッダ (28ファイル)
│   ├── lib/libc/      # ユーザー空間libc
│   │   ├── stdio.c    # printf, putc, puts
│   │   ├── stdlib.c   # atoi
│   │   ├── string.c   # 文字列関数
│   │   ├── malloc.c   # malloc/free
│   │   ├── brk.c      # brk wrapper
│   │   ├── sbrk.c     # sbrk実装
│   │   ├── regex.c    # 正規表現
│   │   ├── inet.c     # ネットワーク
│   │   └── i386/      # syscallスタブ (24 ASMファイル)
│   └── bin/           # ユーザー空間プログラム (16個)
└── net/               # uIP TCP/IPスタック
```

## 付録B: 参考リンク

- musl libc: https://musl.libc.org/
- newlib: https://sourceware.org/newlib/
- POSIX.1-2008仕様: IEEE Std 1003.1
- Linux i386 syscall table: arch/x86/entry/syscalls/syscall_32.tbl
