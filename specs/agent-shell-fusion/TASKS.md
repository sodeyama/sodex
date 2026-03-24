# Agent Shell Fusion Tasks

`specs/agent-shell-fusion/README.md` を、実装単位へ落としたタスクリスト。

## 進捗メモ

- 2026-03-24: `docs/research/agent_shell_fusion_research_2026-03-24.md` を元に spec を新設
- 2026-03-24 時点では、計画作成のみで実装は未着手
- 2026-03-24: M0 の MVP として boot profile syscall、`@terminal`、`init` 解決、`agent-term` fallback、`@...` による明示 agent route、host/QEMU smoke を実装
- 2026-03-24: `eshell` に `/mode auto|shell|agent`、mode badge、shell route probe、agent mode の plain text route を追加
- 2026-03-24: `agent-term` 欠落時に `init` が `/usr/bin/term` へ退避する fallback smoke を追加
- 2026-03-24: M1 として command-not-found recovery、typo/path suggestion、permission/upstream hint、destructive auto-apply deny、host/QEMU 回帰を実装
- 2026-03-24: M2 の MVP として session surface、`/status` `/sessions` `/resume` `/clear` `/compact` `/permissions` `/drawer`、recent command bridge、permission override、audit/session 表示を実装
- 2026-03-24: M3 の MVP として `run_command` proposal stop、command block、`/approve once|session`、`/deny`、approved 実行と compact/recent 反映を実装

## M0: terminal profile 切替と unified input router

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | ASF-01 | `boot_profile.h`、`SYS_CALL_GET_BOOT_PROFILE(124)`、`get_boot_profile()` の ABI を追加する | 01 | なし | kernel/userland 双方で同じ enum/struct を共有できる |
| [x] | ASF-02 | kernel 側に `SODEX_TERM_PROFILE=classic|agent` の build knob を追加し、boot profile を返せるようにする | 01 | ASF-01 | build ごとに terminal profile を切り替えられる |
| [x] | ASF-03 | `inittab` の terminal respawn token `@terminal` を追加し、`init_policy` の既定値を切り替える | 01 | ASF-01 | `/etc/inittab` が kernel profile 非依存の token を使える |
| [x] | ASF-04 | `init` が kernel profile を読んで `@terminal` を `/usr/bin/term` または `/usr/bin/agent-term` に解決するようにする | 01 | ASF-02, ASF-03 | boot ごとに起動する terminal を切り替えられる |
| [x] | ASF-05 | `agent-term` 起動失敗時の `term` fallback と audit log を実装する | 01 | ASF-04 | `agent-term` が壊れても stable terminal へ自動退避できる |
| [x] | ASF-06 | `agent-term` 入力面の mode state (`auto` / `shell` / `agent`) 契約を固定する | 01 | ASF-04 | mode 遷移と line-local override の仕様が文書と実装で一致する |
| [x] | ASF-07 | shell fast path 判定を `agent-term` 側へ追加する | 01 | ASF-06, `shell-and-init` | builtin / alias / external command が LLM を通らず実行される |
| [x] | ASF-08 | agent force の入力導線と mode badge / route reason 表示を追加する | 01 | ASF-06, `agent-transport` | shell / agent へなぜ流れたかをユーザーが視認できる |
| [x] | ASF-09 | boot profile / `@terminal` / fallback / route の host/QEMU test を追加する | 01 | ASF-05, ASF-07, ASF-08 | `classic` / `agent` 切替と fallback を回帰検知できる |

注記:
現時点の QEMU smoke は `agent` profile での login、`agent-term` 起動、明示 agent route (`@memory add ...`) に加え、
`/mode agent` 後の plain text route (`memory add ...`)、
`/mode shell` での typo recovery (`ecoh` -> `echo`) と path recovery (`cd /hme/user` -> `cd /home/user`)、
および `/usr/bin/agent-term` 欠落時の `init -> /usr/bin/term` fallback まで検証する。

## M1: typo 補正と safe correction

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | ASF-10 | command-not-found 時の recovery hook を実装する | 02 | ASF-07 | shell 失敗後に suggestion path へ入れる |
| [x] | ASF-11 | executable / path / history を使う typo suggestion engine を実装する | 02 | ASF-10, `shell-and-init` | `sl` -> `ls` などの代表 typo を候補提示できる |
| [x] | ASF-12 | console error パターンに基づく recovery hint を追加する | 02 | ASF-10 | permission denied, missing upstream などで修正候補が出る |
| [x] | ASF-13 | destructive command の auto-apply 禁止ポリシーを入れる | 02 | ASF-11, ASF-12 | `rm`, `mv`, `git push` 等は suggestion 止まりになる |
| [x] | ASF-14 | typo / recovery の host test と QEMU smoke を追加する | 02 | ASF-11, ASF-12, ASF-13 | candidate ranking と accept path が guest 上で固定される |

## M2: agent session surface と policy control

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | ASF-15 | `agent-term` に agent drawer を追加する | 03 | ASF-08, `rich-terminal` | transcript と status を terminal 内で表示できる |
| [x] | ASF-16 | session id / cwd / context usage / permission mode を drawer に出す | 03 | ASF-15, `agent-transport` | 実行中 session の状態を常時確認できる |
| [x] | ASF-17 | `/clear`, `/compact`, `/permissions`, `/sessions`, `/resume` を drawer 経由で扱えるようにする | 03 | ASF-15, `agent-transport` | `agent` REPL 相当操作を `agent-term` 上で継続できる |
| [x] | ASF-18 | recent command block を agent 文脈へ橋渡しする | 03 | ASF-15, ASF-16 | 直前の shell 実行結果を会話へ再利用できる |
| [ ] | ASF-19 | approval / deny / session-allow と audit 表示を追加する | 03 | ASF-16, `agent-transport` | tool / shell action の許可状態と監査結果が見える |

注記:
現時点の M2 は `term` overlay ではなく、`eshell --agent-fusion` が描く text drawer MVP で実装している。
`/permissions`、`/clear`、`/resume`、recent command bridge は host test で回帰し、
QEMU smoke は boot/profile、明示 agent route、`/status`、typo/path recovery を確認する。

## M3: agent mediated shell actions

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [x] | ASF-20 | agent が shell command proposal を返せる surface を作る | 04 | ASF-15, ASF-19 | command 提案が単なる text でなく実行候補 block として表示される |
| [x] | ASF-21 | `1回許可` / `session許可` / `deny` の approval flow を実装する | 04 | ASF-20, ASF-19 | command 実行前に policy を選べる |
| [x] | ASF-22 | approved command を shell 経由で実行し、bounded output を session に戻す | 04 | ASF-20, ASF-21, `agent-transport` | run_command 出力が terminal と agent session の両方に反映される |
| [ ] | ASF-23 | long-running shell command の attach / detach を追加する | 04 | ASF-22 | tailing / build / test のような長時間 command を扱える |
| [ ] | ASF-24 | shell action の host/QEMU 回帰を追加する | 04 | ASF-22, ASF-23 | approve path と deny path が smoke で固定される |

注記:
現時点の M3 は `run_command` を backend で即実行せず、
`AGENT_STOP_APPROVAL_REQUIRED` と audit `propose` に変換して
`eshell --agent-fusion` の text drawer へ proposal block を出す。
承認済み command 実行は `tool_run_command` の bounded capture を再利用し、
recent block と session compact summary に反映する。

## M4: interactive PTY observe / attach

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [ ] | ASF-25 | foreground PTY の observe-only attach を実装する | 05 | ASF-15, ASF-22 | running app の viewport / recent output を agent が読める |
| [ ] | ASF-26 | agent session 用の簡易 prompt / 環境変数契約を入れる | 05 | ASF-25, `shell-and-init` | heavy prompt を避ける child shell mode が使える |
| [ ] | ASF-27 | PTY write を approval 付きに限定して実装する | 05 | ASF-25, ASF-21 | interactive app へ agent が直接入力する前に確認が出る |
| [ ] | ASF-28 | interactive app 種別ごとの attach policy を作る | 05 | ASF-25 | `less`, `python`, `psql`, `gdb`, `vi` などを区別できる |
| [ ] | ASF-29 | PTY attach の host/QEMU smoke を追加する | 05 | ASF-26, ASF-27, ASF-28 | observe / write-block / approve-write が回帰検知できる |

## M5: `vi` の agent-native integration

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [ ] | ASF-30 | `vi` の current buffer / visual selection export を実装する | 06 | ASF-15, `rich-terminal` | 範囲選択を agent へ安全に渡せる |
| [ ] | ASF-31 | `:AgentAsk`, `:AgentEdit`, `:AgentFix`, `:AgentReview` を追加する | 06 | ASF-30, ASF-15 | `vi` から agent command を起動できる |
| [ ] | ASF-32 | `vi` 内 diff preview と accept / reject 導線を追加する | 06 | ASF-31 | 編集提案を適用前に確認できる |
| [ ] | ASF-33 | apply 後の undo/redo 整合を固定する | 06 | ASF-32 | 1 回の agent apply が `u` / redo と矛盾しない |
| [ ] | ASF-34 | `vi` agent 統合の host/QEMU test を追加する | 06 | ASF-31, ASF-32, ASF-33 | selection edit と review flow を guest 上で固定できる |

## M6: validation / rollout / guardrails

| 状態 | ID | タスク | Plan | 主な依存 | 完了条件 |
|---|---|---|---|---|---|
| [ ] | ASF-35 | kernel profile 既定値と userland feature flag を整理する | 07 | 01-06 | `classic` / `agent` 切替と部分 rollout が両立する |
| [ ] | ASF-36 | shadow routing と metrics を入れて誤ルーティングを観測する | 07 | ASF-09, ASF-14 | 実行せず classification だけ採る観測モードがある |
| [ ] | ASF-37 | redraw / latency / command block 量の性能計測を追加する | 07 | 01-06, `terminal-view-performance` | overlay 導入後も `agent-term` 性能が劣化しないことを確認できる |
| [ ] | ASF-38 | E2E smoke (`make` ターゲット含む) を追加する | 07 | ASF-24, ASF-29, ASF-34 | login -> terminal profile -> shell -> agent -> PTY -> `vi` の代表導線を 1 コマンドで検証できる |
| [ ] | ASF-39 | README / guest help / 操作説明を更新する | 07 | ASF-35, ASF-38 | 操作体系、制約、既知非対応が docs と一致する |
