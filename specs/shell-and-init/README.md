# Shell and Init Spec

`sodex` に、text ベースの shell script 実行系と、
`/etc/init.d/` による boot-time service 起動モデルを導入する計画。

狙いは「kernel boot 後に userland daemon を script から起動する」経路を作り、
`/usr/bin/sshd` のような常駐 process を hardcode ではなく service script で起動すること。

この spec は shell / init / daemon 起動の共通基盤を扱う。
`SSH` protocol 自体の詳細は `specs/userland-ssh-server/` を優先する。

## 背景

現状の `sodex` では、boot 後の userland 起動は `src/usr/init.c` に
直書きされており、`/usr/bin/sshd` と `term` の起動順や条件を
script で差し替えることができない。

また、既存の `eshell` は対話 1 行入力向けの最小 shell であり、
次の用途にはまだ足りない。

- text file からの non-interactive 実行
- shell 変数 / positional parameter / `export`
- `;`, `<newline>`, `&&`, `||`, `&` を含む list 実行
- `wait`, `trap`, `.` のような current shell 環境を変える built-in
- `service start/stop/status` を書ける background / daemon 制御

さらに process 側も、現在は次の制約がある。

- `execve()` が Unix の「自己置換」ではなく spawn 的な挙動
- `sys_fork()` が stub
- `waitpid()` が単一 pid の busy-wait に近く、`WNOHANG` や `-1` 未対応
- script 実行の `#!` / `ENOEXEC` fallback がない
- `init` が child reaper / service supervisor の責務を持っていない

## ゴール

- `/usr/bin/sh` が text file または `-c` から shell script を実行できる
- `/usr/bin/init` が boot 後に `/etc/init.d/rcS` を実行する
- `/etc/init.d/sshd start` で `/usr/bin/sshd` を background daemon として起動できる
- `ps` に `/usr/bin/sshd` が見え、`service sshd status` 相当で状態確認できる
- generic な shell script を guest 内で書いて実行できる
- 現在 `init.c` に直書きされている service 起動を script 側へ段階移行できる

## 非ゴール

- 初回から POSIX shell 全量を完装しない
- 最初から SysV runlevel 全体、`/etc/inittab` 全量、login manager まで広げない
- いきなり full job control、`fg` / `bg` / `jobs` を必須にしない
- `SSH` protocol 実装そのものをこの spec で再設計しない
- multi-user permission model や ownership を同時に完成させない

## 外部調査からの要求

2026-03-16 の一次資料調査で、最低でも次を満たす必要があると整理した。

### 1. shell は list / async / current shell built-in を持つ必要がある

POSIX Shell Command Language では、
`&` による asynchronous list、
`;` と `<newline>` による sequential list、
`&&` / `||` による AND-OR list が shell の基本構文になっている。

また `.`、`trap`、`wait`、`exit` のように
current shell execution environment を直接変える built-in がある。
このため、単なる「command launcher」ではなく、
親 shell の状態を持つ interpreter が必要になる。

### 2. shell script 実行は text file を前提にする

`sh` は text file を入力として読み、
line length 無制限を前提にする。
さらに command search/exec では、
`ENOEXEC` 相当時に shell で text file を再実行する規定がある。

したがって、`sh script.sh` だけでなく、
将来的には `./script.sh` や `foo` の direct 実行を
`#!` または `ENOEXEC` fallback で扱う設計が必要になる。

### 3. init script には action 契約と exit status 契約が必要

LSB の init script 規約では、
少なくとも `start`, `stop`, `restart`, `force-reload`, `status`
を全 script が受け付け、
`status` には共通の exit status を返すことが推奨されている。

また init script は非標準の `PATH` や `USER` に依存せず、
既に起動済み / 未起動でも sensibly に振る舞う必要がある。

`sodex` でも、`/etc/init.d/sshd` を入れるなら
同様の action / exit 契約を最初から固定した方がよい。

## 現状との差分

### 既にあるもの

- `/usr/bin/eshell` の最小対話 shell
- pipe と `<`, `>`, `>>`
- `execve()`, `waitpid()`, `kill()`, signal の最小骨格
- `ps`, `kill`, `pwd`, `cd`, `mkdir`, `rm` などの userland command
- ext3 rootfs と overlay 注入
- `/usr/bin/sshd` 自体の build と実行

### まだ足りないもの

- `/usr/bin/sh` と non-interactive script 実行
- shell AST と current shell built-in
- background 実行と既知 child pid table
- `waitpid(-1)` / `WNOHANG` / child reap
- daemon の detach / pidfile / status helper
- `/etc/init.d/rcS` を実行する init
- hardcode された `sshd` 起動の service script 化

## 設計方針

### 1. `eshell` と `sh` は共通 core を使う

別々の shell を育てると保守が重いので、
parser / expansion / executor は共通ライブラリ化する。

- `eshell`: interactive frontend
- `sh`: file / `-c` 実行 frontend

この形なら、script 対応で得た parser / executor 改善を
対話 shell にも戻せる。

### 2. spawn と exec の契約を整理する

現状の `execve()` は実質 spawn なので、
そのまま POSIX shell を載せると概念がぶれる。

そのため初期 phase では、次のどちらかへ寄せる。

1. 現在の挙動を `spawnve()` / `posix_spawn()` 系 API として明示化し、
   `execve()` は後方互換レイヤか段階移行対象にする
2. 先に `execve()` を自己置換へ寄せ、spawn 系を新設する

どちらを選ぶにしても、
shell / init / service helper からは
「親を置換する exec」と「子を増やす spawn」を分けて扱う。

### 3. daemon 起動は shell `&` だけに依存しすぎない

generic shell には `cmd &` が必要だが、
service script の実装まで raw shell に寄せすぎると、
pidfile、status、stdio redirect、detach の責務が script に散る。

そのため次を併用する。

- shell としての `&`, `$!`, `wait`, `trap`
- service 向けの最小 helper
  - 例: `/usr/bin/service`
  - 例: `/usr/bin/start-stop-daemon` 互換の最小版

script はこの helper を使って
`start/stop/status` の挙動を揃える。

### 4. boot sequence はまず `rcS` 1 本から始める

最初から runlevel や `install_initd` まではやらず、
MVP は次でよい。

1. kernel が `/usr/bin/init` を起動する
2. `init` が既定環境を整える
3. `init` が `/usr/bin/sh /etc/init.d/rcS` を実行する
4. `rcS` が `/etc/init.d/sshd start` などを呼ぶ
5. `init` は child を reap し続ける

`/etc/init.d/` という配置は最初から採るが、
実行順は `rcS` の中で明示し、runlevel metadata は後段に回す。

## 後続拡張

2026-03-17 時点で、MVP の後段として次まで入っている。

- `### BEGIN INIT INFO` の `Provides` / `Required-Start` / `Default-Start` を解釈し、`rc-order` で service 順序を決める
- `/etc/inittab` で `initdefault`, `sysinit`, `respawn` を解釈し、`user` と `server-headless` を切り替える
- interactive shell で `jobs`, `fg`, `bg` を使える

default rootfs は `user` runlevel で `term` を起動し、
server 用 overlay は `server-headless` に切り替えて `sshd` を起動する。

さらに hardening と failure-path も次まで固定済み。

- `waitpid(-1)` は sleep/wakeup で PID1 reaper の busy-loop を避ける
- `start-stop-daemon` は `SIGTERM` + timeout, `force-reload`,
  stale pidfile cleanup, prerequisite check を持つ
- `rcS` failure 時は `rescue` respawn へフォールバックし、
  failure-path の QEMU smoke で回帰を固定する

## フェーズ

| # | ファイル | 概要 | 主な依存 |
|---|---|---|---|
| 01 | [plans/01-process-model-and-spawn-contract.md](plans/01-process-model-and-spawn-contract.md) | shell/init 用の process 契約を整理する | なし |
| 02 | [plans/02-shell-core-and-parser.md](plans/02-shell-core-and-parser.md) | 共通 shell core と parser/AST を作る | 01 |
| 03 | [plans/03-script-execution-and-builtins.md](plans/03-script-execution-and-builtins.md) | script 実行、変数、built-in、`#!`/fallback を導入する | 01, 02 |
| 04 | [plans/04-background-daemons-and-service-helpers.md](plans/04-background-daemons-and-service-helpers.md) | background 実行、daemon helper、pidfile/status を入れる | 01, 02, 03 |
| 05 | [plans/05-boot-init-and-rcs.md](plans/05-boot-init-and-rcs.md) | `/etc/init.d/rcS` と boot init を成立させる | 01, 02, 03, 04 |
| 06 | [plans/06-sshd-service-cutover-and-tests.md](plans/06-sshd-service-cutover-and-tests.md) | `sshd` を service script 起動へ切り替え、回帰を固定する | 04, 05, `specs/userland-ssh-server/` |

実装タスクは [TASKS.md](TASKS.md) で管理する。

## 主なリスク

- `execve()` の意味を変えると既存 userland の前提が崩れやすい
- `fork()` を急いで実装すると paging / fd 複製 / signal で広がりやすい
- parser を一気に POSIX 全量へ広げると regress しやすい
- `init` が child を reap しないと zombie が増える
- daemon detach を雑にやると TTY / signal / stdin が絡んで不安定になりやすい
- `service` helper を作らず shell だけで押すと `status` / `restart` が散らばる

## 変更候補

- 既存
  - `src/usr/init.c`
  - `src/usr/command/eshell.c`
  - `src/usr/lib/libc/eshell_parser.c`
  - `src/execve.c`
  - `src/process.c`
  - `src/signal.c`
  - `src/syscall.c`
  - `src/usr/include/stdlib.h`
  - `src/usr/lib/libc/i386/execve.S`
  - `src/usr/lib/libc/i386/fork.S`
  - `src/usr/lib/libc/i386/waitpid.S`
  - `src/test/run_qemu_ssh_smoke.py`
- 新規候補
  - `src/usr/command/sh.c`
  - `src/usr/lib/libc/shell_*.c`
  - `src/usr/include/shell.h`
  - `src/usr/command/service.c`
  - `src/usr/command/daemonize.c`
  - `src/usr/include/spawn.h`
  - `src/usr/lib/libc/i386/spawn*.S`
  - `src/test/run_qemu_init_rc_smoke.py`
  - `src/test/run_qemu_service_smoke.py`
  - `src/test/rootfs-overlay/init/`

## 完了条件

- [x] `/usr/bin/sh` で text shell script を実行できる
- [x] `/etc/init.d/rcS` が boot 時に動き、service script を呼べる
- [x] `/etc/init.d/sshd start` で `sshd` が background 起動する
- [x] `status` / `restart` の挙動と exit code が文書化され、smoke で固定される
- [x] `src/usr/init.c` の hardcode 起動を段階撤去できる
- [x] interactive `eshell` と non-interactive `sh` が共通 core を使う
- [x] LSB metadata に基づく service 順序と `server-headless` runlevel を使える
- [x] interactive shell の最小 job control (`jobs`, `fg`, `bg`) を smoke で固定する
- [x] `force-reload`, stale pidfile, prerequisite failure, `status=3|4` が contract smoke で固定される
- [x] `rcS` failure 時に rescue へフォールバックし、PID1 reaper が idle で busy-loop しない

## 参考資料

- POSIX Shell Command Language
  - https://pubs.opengroup.org/onlinepubs/9799919799/utilities/V3_chap02.html
- POSIX `sh`
  - https://pubs.opengroup.org/onlinepubs/9799919799/utilities/sh.html
- POSIX `wait`
  - https://pubs.opengroup.org/onlinepubs/9699919799/utilities/wait.html
- POSIX `trap`
  - https://pubs.opengroup.org/onlinepubs/9799919799/utilities/trap.html
- POSIX `exec` / `posix_spawn`
  - https://pubs.opengroup.org/onlinepubs/9799919799/functions/exec.html
  - https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn.html
- LSB Init Script Actions
  - https://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/iniscrptact.html
- LSB Init Script Installation / Comment Conventions
  - https://refspecs.linuxfoundation.org/LSB_4.0.0/LSB-Core-generic/LSB-Core-generic/initsrcinstrm.html
  - https://refspecs.linuxfoundation.org/LSB_3.2.0/LSB-Core-generic/LSB-Core-generic/initscrcomconv.html
