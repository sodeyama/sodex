# Shell and Init Tasks

`specs/shell-and-init/README.md` を、実装単位に落としたタスクリスト。

## 進捗メモ

- 2026-03-16: plans 01-06 を実装し、host test / QEMU smoke (`test-qemu-init-rc`, `test-qemu-service`) を通過
- 2026-03-17: M5 を実装し、LSB metadata / `inittab` runlevel / `jobs` `fg` `bg` を追加した
- default rootfs は `user` runlevel の `term` を維持し、server overlay は `server-headless` で `sshd` を起動する
- `init` は `/etc/init.d/rcS` を起動し、`sshd` は service script 経由で起動する
- `sh` / `eshell` は共通 shell core を使い、service helper と smoke で回帰を固定した

## M0: process 契約と shell core の土台

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SIS-01 | shell/init 用の process 契約を固定する | なし | `spawn` と `exec` の使い分け、child reaper、compat 方針が文書化される |
| [x] | SIS-02 | `waitpid(-1)` / `WNOHANG` / child reap を追加する | SIS-01 | `init` と shell が block せず child を回収できる |
| [x] | SIS-03 | 共通 shell core の骨格と `/usr/bin/sh` を追加する | SIS-01 | `eshell` と `sh` が同じ parser/executor を共有できる |

## M1: shell language MVP

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SIS-04 | parser を list/AND-OR/background 対応へ拡張する | SIS-03 | `;`, `<newline>`, `&&`, `||`, `&` を parse できる |
| [x] | SIS-05 | shell 変数と基本 built-in を実装する | SIS-03, SIS-04 | `cd`, `exit`, `export`, `set`, `.`, `wait`, `trap` が使える |
| [x] | SIS-06 | pipeline 実行を親 wait 直列から実際の multi-child 実行へ直す | SIS-01, SIS-03, SIS-04 | `cmd1 | cmd2` が deadlock しない |
| [x] | SIS-07 | `sh file`, `sh -c`, positional parameter, `$?`, `$!` を実装する | SIS-03, SIS-04, SIS-05 | non-interactive shell script が書ける |
| [x] | SIS-08 | `#!` または `ENOEXEC` fallback による direct script 実行を導入する | SIS-01, SIS-07 | `./script.sh` または `foo` が shell script として起動できる |

## M2: daemon / service helper

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SIS-09 | shell の background 実行と known pid table を実装する | SIS-02, SIS-04, SIS-07 | `cmd &` の pid を shell が保持し `wait` できる |
| [x] | SIS-10 | service 向け helper を追加する | SIS-02, SIS-07, SIS-09 | `start/stop/status` を script から共通化できる |
| [x] | SIS-11 | pidfile / `kill -0` / status exit code を整備する | SIS-10 | LSB 風の `status` 契約を返せる |
| [x] | SIS-12 | daemon detach の最小基盤を入れる | SIS-01, SIS-10 | 新 process group / session、stdio redirect、TTY 切り離しができる |

## M3: boot init と `/etc/init.d`

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SIS-13 | `/usr/bin/init` を `rcS` 実行 + child reap ループへ変更する | SIS-02, SIS-07 | boot 後に `/etc/init.d/rcS` が動く |
| [x] | SIS-14 | `/etc/init.d/rcS`, `rc.common`, `service` 呼び出し規約を固定する | SIS-10, SIS-13 | service script の配置と action 契約が文書・fixture で固定される |
| [x] | SIS-15 | `/etc/init.d/sshd` を追加し hardcode 起動を置き換える | SIS-11, SIS-12, SIS-13, `specs/userland-ssh-server/` | `init.c` 直書きなしで `sshd` が boot 起動する |
| [x] | SIS-16 | interactive `term` の起動責務を整理する | SIS-13 | `user` / `server-headless` で `term` と service 起動の責務が衝突しない |

## M4: 検証と移行

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SIS-17 | host unit test を追加する | SIS-04, SIS-05, SIS-07 | parser / expansion / executor を host 側で回帰できる |
| [x] | SIS-18 | QEMU smoke を追加する | SIS-13, SIS-15 | boot 時 `rcS` 実行、`sshd` 自動起動、service action が smoke で確認できる |
| [x] | SIS-19 | manual 手順と overlay 導線を README/spec に反映する | SIS-15, SIS-18 | `bin/restart.sh ...` と guest 内 script 手順が文書化される |

## M5: 後続候補

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SIS-20 | LSB comment block と依存順 metadata を解釈する | SIS-14 | `### BEGIN INIT INFO` を読んで順序付けできる |
| [x] | SIS-21 | `/etc/inittab` または runlevel 相当を導入する | SIS-13, SIS-20 | `rcS` 1 本より細かい policy を持てる |
| [x] | SIS-22 | `fg` / `bg` / `jobs` などの job control を検討する | SIS-09, SIS-12 | interactive shell の background job をより Unix 風に扱える |
