# Shell and Init Tasks

`specs/shell-and-init/README.md` を、実装単位に落としたタスクリスト。

## 進捗メモ

- 2026-03-16: plans 01-06 を実装し、host test / QEMU smoke (`test-qemu-init-rc`, `test-qemu-service`) を通過
- 2026-03-17: M5 を実装し、LSB metadata / `inittab` runlevel / `jobs` `fg` `bg` を追加した
- 2026-03-17: 追加調査で、`waitpid(-1)` の busy-loop、daemon detach の不足、`rcS` failure path、`sshd` service の前提チェック不足を後続 hardening 項目として洗い出した
- 2026-03-17: M6 を実装し、`waitpid(-1)` の sleep/wakeup 化、`start-stop-daemon` の graceful stop / `force-reload` / prerequisite check、`rcS` failure rescue、failure-path smoke を追加した
- 2026-03-19: `bin/start.sh --ssh` / `bin/restart.sh --ssh` が、そのモード向け overlay を自動で再生成してから起動するようにし、plain build 後に rescue shell へ落ちる導線を塞いだ
- 2026-03-22: Plan 07 / M7 として、programmable shell 向けの制御構文、`test`、継続 prompt の計画を追加した
- 2026-03-22: M7 を実装し、compound AST、`if` / loop / `break` / `continue`、最小 `test` / `[`, `eshell` 継続 prompt、host test、boot-time `sh` / redirected `eshell` の QEMU smoke を追加した
- 2026-03-22: 追加調査で、`alias`, `type` / `command -v`, interactive history, `~`, glob が次の shell usability gap と分かり、Plan 08 / M8 候補へ切り出した
- 2026-03-22: M8 を実装し、`alias` / `unalias`, `type` / `command -v`, session history, `!!`, `!prefix`, `~`, glob、host test、boot-time `sh` / redirected `eshell` の QEMU smoke を追加した
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
| [x] | SIS-12 | daemon detach の最小基盤を入れる | SIS-01, SIS-10 | stdio redirect と TTY 切り離しで daemon を対話端末から分離できる |

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

## M6: hardening と failure-path

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SIS-23 | `waitpid(-1)` を sleep/wakeup 化して PID1 reaper の busy-loop を止める | SIS-02, SIS-13 | child が残っていても zombie が無い間は `init` が CPU を無駄に回さない |
| [x] | SIS-24 | `start-stop-daemon` に stdin / TTY 切り離しと `SIGTERM` + timeout を入れる | SIS-10, SIS-12 | daemon stop が graceful になり、TTY 依存を引きずらない |
| [x] | SIS-25 | service action / exit status 契約を補完する | SIS-10, SIS-11, SIS-24 | `force-reload` と stale pidfile / `status=3|4` の境界が文書・実装でそろう |
| [x] | SIS-26 | `rcS` 失敗時ポリシーと `sshd` 前提チェックを固定する | SIS-13, SIS-15 | boot failure の挙動と `/etc/sodex-admin.conf` 前提が script / smoke で一致する |
| [x] | SIS-27 | failure-path の host/QEMU test を追加する | SIS-23, SIS-24, SIS-25, SIS-26 | `stop`, not running, stale pidfile, `rcS` failure が回帰で固定される |

## M7: programmable shell 制御構文

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SIS-28 | shell AST を compound command 対応へ拡張し、incomplete input を parse error と分離する | SIS-03, SIS-04, SIS-05 | `if` / loop 用 node と継続入力判定を共通 core で持てる |
| [x] | SIS-29 | tokenizer / parser を更新し、予約語と `if` / `elif` / `else` / `fi`, `for ... in`, `while` / `until`, `do` / `done` を解釈できるようにする | SIS-28 | file 実行で compound command を parse できる |
| [x] | SIS-30 | executor に条件評価、loop 反復、`break` / `continue` を追加し、current shell 状態を保ったまま compound command を実行できるようにする | SIS-28, SIS-29, SIS-05 | 条件分岐と繰り返しが exit status と shell 変数を保って動く |
| [x] | SIS-31 | 最小 `test` / `[` 実装を追加し、文字列比較、空判定、代表 file 判定で条件を書けるようにする | SIS-03, SIS-05 | `if test -n \"$x\"; then ...` と `if [ -f foo ]; then ...` が成立する |
| [x] | SIS-32 | `eshell` に continuation prompt と multi-line buffer を追加し、対話でも compound command を入力できるようにする | SIS-28, SIS-29 | `if` と loop を対話 shell で完結入力できる |
| [x] | SIS-33 | host unit test を拡張し、AST、実行、`test` helper、継続入力判定の回帰を固定する | SIS-29, SIS-30, SIS-31 | 制御構文の主要分岐と loop を host 側で回帰検知できる |
| [x] | SIS-34 | QEMU smoke を追加し、`sh` script と `eshell` の両方で条件分岐、繰り返し、変数利用、`break` / `continue` を固定する | SIS-30, SIS-31, SIS-32, SIS-33 | guest 上の代表 script workflow を回帰検知できる |
| [x] | SIS-35 | README / spec / guest 内サンプル script を更新し、使い方と非対応範囲を明文化する | SIS-34 | 使用例と制約が docs と smoke で一致する |

## M8: shell 使い勝手と展開

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | SIS-36 | alias table と command resolution helper を追加し、alias / builtin / external の lookup 順序を固定する | SIS-03, SIS-05, SIS-07 | shell 本体と `type` 系が同じ lookup 経路を使える |
| [x] | SIS-37 | `alias` / `unalias` builtin を実装し、command position の先頭 word に最小 alias 展開を入れる | SIS-36 | current shell 状態で alias を定義・参照・削除できる |
| [x] | SIS-38 | `type` / `command -v` を追加し、alias / builtin / external / not found を表示できるようにする | SIS-36, SIS-37 | lookup 結果を対話確認できる |
| [x] | SIS-39 | interactive history buffer と `history` builtin を追加する | SIS-03, SIS-05, SIS-32 | current shell session の履歴を保持・表示できる |
| [x] | SIS-40 | `!!` / `!prefix` の最小 history 展開を interactive shell に追加する | SIS-39 | 直前再実行と prefix 再実行が使える |
| [x] | SIS-41 | `~` 展開と pathname glob (`*`, `?`) を word expansion 層へ追加する | SIS-05, SIS-07 | quote を壊さず basic expansion が使える |
| [x] | SIS-42 | host unit test を追加し、alias / lookup / history / expansion の回帰を固定する | SIS-37, SIS-38, SIS-40, SIS-41 | 主要経路を host 側で回帰検知できる |
| [x] | SIS-43 | QEMU smoke を追加し、redirected `eshell` と script 経由で alias / history / expansion を固定する | SIS-37, SIS-38, SIS-40, SIS-41, SIS-42 | guest 上の代表的な日常利用経路を回帰検知できる |
| [x] | SIS-44 | README / spec / 制約事項を更新し、completion との関係と未対応機能を明文化する | SIS-43 | docs と実装の範囲が一致する |
