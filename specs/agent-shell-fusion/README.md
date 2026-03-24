# Agent Shell Fusion Spec

`docs/research/agent_shell_fusion_research_2026-03-24.md` を、
Sodex 実装向けの spec に落としたもの。

この spec は、login 後の terminal を
「shell と agent が自然に同居する terminal client」
へ拡張するための統合計画を扱う。

既存の shell、PTY、`agent` CLI、`vi`、permission / session を再利用しつつ、
新規 `/usr/bin/agent-term` を追加し、
入力ルーティング、suggestion、approval、interactive attach、
editor-native integration を段階導入する。

## 実装済み MVP

2026-03-24 時点で、M0 の土台となる最小実装は入っている。

- `SYS_CALL_GET_BOOT_PROFILE(124)` と `boot_profile.h`
- `SODEX_TERM_PROFILE=classic|agent` による kernel build-time 切替
- `@terminal` を使う `inittab` と `init` の profile 解決
- `/usr/bin/agent-term` 追加と `/usr/bin/term` / `/usr/bin/eshell` への fallback
- `term --agent-fusion` と `eshell --agent-fusion`
- `@...` を明示 agent route として `agent` CLI へ橋渡しする MVP
- `/mode auto|shell|agent` と mode badge
- shell route probe による `shell builtin` / `shell path` / `unknown command` の reason 表示
- command-not-found recovery、typo/path suggestion、permission/upstream hint
- destructive command の auto-apply deny
- `term_session_surface` による session surface と text drawer MVP
- `/status` `/sessions` `/resume` `/clear` `/compact` `/permissions` `/drawer`
- recent shell command block の prompt bridge
- `agent --perm-mode=<mode>` と `agent --resume <session-id> <prompt>` の単発実行
- `run_command` を approval-required proposal として止める backend hook
- command proposal block と `/approve once|session` `/deny`
- approved command の bounded 実行結果を recent block / compact summary へ反映
- host unit test と `make test-qemu-agent-fusion` による route/fallback/recovery を含む QEMU smoke

未実装:

- term overlay としての drawer 本体
- button-like approval UI と proposal edit 導線
- long-running command attach / detach
- PTY observe / attach
- `vi` agent-native command

注記:
現時点の session surface は `term` の下部 overlay ではなく、
`eshell --agent-fusion` が prompt 近傍へ描く text drawer MVP として実装している。
approval / deny / session-allow は未着手で、次の M3 で扱う。

## この spec の位置づけ

本 spec は新しい shell や新しい agent transport をゼロから作るものではない。
責務は次の 4 つに絞る。

- `agent-term` 上で shell と agent をどう共存させるか
- agent が shell / PTY / `vi` にどう安全に介入するか
- 既存 `agent` backend を terminal UX にどう前面化するか
- 既存 `/usr/bin/term` と新規 `/usr/bin/agent-term` を kernel 設定でどう切り替えるか

依存する既存 spec:

- `specs/rich-terminal/`
  - terminal client、PTY/TTY、input event、overlay、resize
- `specs/shell-and-init/`
  - shell parser、history、alias、glob、job control の土台
- `specs/agent-transport/`
  - agent loop、session、permissions、audit、REPL、memory
- `specs/terminal-view-performance/`
  - overlay / redraw / differential rendering の性能制約

## 背景

Sodex はすでに以下を持っている。

- `term` による rich terminal
- PTY ベースの shell
- `agent` CLI と session / permission / audit
- `vi` と visual selection

一方、今はこれらが別々に存在しており、
login 後に「同じ terminal から shell と agent を自然に使う」体験にはなっていない。

さらに rollout 観点では、
既存 `term` を直接壊す形より、
**既存 `term` を安定版として残し、新規 `agent-term` を別バイナリで育てる**方が安全である。

今不足しているのは主に次である。

- 同じ入力面での shell / agent ルーティング
- command-not-found / typo 回復
- agent session を `term` 上で見せる surface
- agent が提案した shell command の approval / 実行導線
- running PTY への observe / attach
- `vi` での editor-native agent command

## ゴール

- login 後の terminal で、同じ入力面から shell と agent を扱える
- 既存 `/usr/bin/term` と新規 `/usr/bin/agent-term` を boot ごとに切り替えられる
- shell fast path は LLM を通さず、既存 shell の決定性を保つ
- command-not-found / typo では inline suggestion を出せる
- agent 会話、permission mode、approval、command proposal を `agent-term` 上で扱える
- agent が非 interactive shell command を提案・承認・実行できる
- interactive CLI に observe-first で attach できる
- `vi` に `:AgentAsk`, `:AgentEdit`, `:AgentFix`, `:AgentReview` を導入できる

## 非ゴール

- shell 全体を LLM に置き換えない
- destructive command を無言で自動実行しない
- 全ての自然言語入力を強制的に agent へ流さない
- 初回から full autonomous PTY write を既定にしない
- GUI ウィンドウシステムやマウス主体 UI を導入しない
- `vi` を PTY 注入だけで操作する設計を正道にしない

## 設計原則

### 1. shell fast path を守る

`ls`, `grep`, `find`, pipe, redirect, env 展開のような deterministic path は、
既存 shell に直行させる。
この spec で agent に渡すのは次の系統だけに寄せる。

- 明示的な agent 要求
- command-not-found / typo 回復
- 自然言語タスク
- agent が提案した shell action

### 2. 自律性より権限を先に作る

approval、permission mode、audit が揃う前に autonomy を増やさない。
特に write / process / network を伴う操作では、
「1 回許可」「session 許可」「deny」を明示的に扱う。

### 3. interactive attach は observe-first にする

running PTY へ入るときは、まず read-only observation と suggestion を作る。
PTY write は次段で approval 付きに限定する。

### 4. `vi` は editor-native integration を優先する

`vi` には selection export、diff preview、accept / reject の導線を用意し、
PTY への key 注入は補助経路に留める。

### 5. terminal 性能を壊さない

overlay、drawer、suggestion は
`specs/terminal-view-performance/` の制約内で実装する。
常時 full redraw や scrollback 全再描画を避ける。

### 6. 既存 `term` は残し、切替は kernel 設定で行う

この spec では、
既存 `/usr/bin/term` を直接 agent 融合版へ置換しない。

- `/usr/bin/term`
  - 現行の stable terminal
- `/usr/bin/agent-term`
  - 新規の shell+agent 融合 terminal

boot 時の起動対象は、userland の `inittab` 直書きではなく、
**kernel が公開する terminal profile** を `init` が参照して決める。

初期 profile:

- `classic`
  - `/usr/bin/term`
- `agent`
  - `/usr/bin/agent-term`

profile の source of truth は kernel 側に置く。
実装順としては、
まず build-time define か boot-time config として kernel が保持し、
その後に必要なら boot arg 化してもよい。

## boot-time terminal profile 契約

### 目的

- 既存 `term` を常に退避経路として残す
- agent 融合 terminal の rollout を opt-in にできる
- QEMU smoke と普段使いで同じ切替軸を使える

### MVP 契約

kernel は boot 時に terminal profile を 1 つ持つ。

- `classic`
- `agent`

`init` は respawn 対象を直接 `/usr/bin/term` に決め打ちせず、
kernel から受け取った profile をもとに次を選ぶ。

- `classic` -> `/usr/bin/term`
- `agent` -> `/usr/bin/agent-term`

`/etc/inittab` では、terminal respawn command を直接 path で持たず、
予約トークン `@terminal` で表す。

例:

```text
initdefault:user
sysinit:/etc/init.d/rcS
respawn:user:@terminal
respawn:rescue:/usr/bin/eshell
```

`init` は `@terminal` を見たときだけ kernel profile を解決し、
通常の path が書かれている場合はその path を優先する。

これにより、

- 普段の boot は kernel profile で切替
- 特殊 smoke や overlay は path 直書きで強制

を両立できる。

### 実装形

MVP の候補は次のどちらかでよい。

1. kernel build-time 設定
   - 例: `SODEX_TERM_PROFILE=classic|agent`
2. boot-time config を kernel が保持し、userland から syscall で読む

この spec では 2 を最終形に寄せる。
理由は、`inittab` overlay だけに依存すると
「kernel 設定で切り替える」という要求を満たしにくいため。

## 具体 ABI

### syscall

getter 系 syscall と同じ形で、次を追加する。

- `SYS_CALL_GET_BOOT_PROFILE 124`

kernel 側:

- `src/include/sys/syscalldef.h`
- `src/syscall.c`

userland 側:

- `src/usr/include/sys/syscall.h`
- `src/usr/lib/libc/i386/get_boot_profile.S`
- `src/usr/include/boot_profile.h`

### userland header

```c
/* src/usr/include/boot_profile.h */

#define BOOT_PROFILE_VERSION 1

enum boot_terminal_profile {
  BOOT_TERM_CLASSIC = 0,
  BOOT_TERM_AGENT = 1
};

struct boot_profile_info {
  u_int32_t version;
  u_int32_t terminal_profile;
  u_int32_t flags;
  u_int32_t reserved;
};

int get_boot_profile(struct boot_profile_info *out);
```

`flags` は将来拡張用に確保し、MVP では 0 固定でよい。

### kernel 側実装

kernel は `boot_profile_info` を 1 つ保持し、
`sys_get_boot_profile(struct boot_profile_info *out)` で userland へコピーする。

MVP では build-time define で初期化する。

例:

```c
#ifndef SODEX_TERM_PROFILE
#define SODEX_TERM_PROFILE BOOT_TERM_CLASSIC
#endif
```

### build knob

build / QEMU / smoke の共通 knob として次を使う。

- `SODEX_TERM_PROFILE=classic|agent`

`src/makefile` で kernel CFLAGS へ落とし、
QEMU smoke と `bin/start.sh` でも同じ名前を使う。

既存の `SODEX_INITDEFAULT_RUNLEVEL` とは別軸にする。

## init 側の解決規則

### `init_policy` の既定値

`init_inittab_init()` の既定 respawn command は
`/usr/bin/term` ではなく `@terminal` に変える。

### `init` の解決順

`init_select_respawn()` の後、実際の spawn 前に
次の resolve を行う。

1. respawn command が `@terminal` なら kernel profile を読む
2. `classic` なら `/usr/bin/term`
3. `agent` なら `/usr/bin/agent-term`
4. syscall 失敗時は `classic` 扱い

### exec fallback

`/usr/bin/agent-term` の起動に失敗した場合は、
`init` が即座に `/usr/bin/term` へフォールバックする。

さらに `/usr/bin/term` も失敗した場合だけ、
既存の rescue 経路へ落とす。

順序:

1. `agent-term`
2. `term`
3. `eshell` rescue

### audit log

少なくとも次を出す。

```text
AUDIT init_term_profile=classic
AUDIT init_term_profile=agent
AUDIT init_terminal_resolved=/usr/bin/term
AUDIT init_terminal_resolved=/usr/bin/agent-term
AUDIT init_terminal_fallback=/usr/bin/term
```

## 目標アーキテクチャ

```text
              ┌─────────────────────────────────┐
              │ agent-term                      │
              │  - input router                 │
              │  - mode badge                   │
              │  - suggestion line              │
              │  - agent drawer                 │
              │  - approval prompt              │
              └──────────────┬──────────────────┘
                             │
              ┌──────────────┼──────────────────┐
              │              │                  │
      ┌───────▼────────┐ ┌───▼────────────┐ ┌──▼─────────────┐
      │ shell core     │ │ libagent       │ │ vi bridge      │
      │  - parser      │ │  - session     │ │  - selection   │
      │  - executor    │ │  - permissions │ │  - diff apply  │
      │  - history     │ │  - audit       │ │  - ex command   │
      │  - completion  │ │  - run_command │ │                 │
      └───────┬────────┘ └───┬────────────┘ └──┬─────────────┘
              │              │                  │
              └───────┬──────┴──────────┬───────┘
                      │                 │
                ┌─────▼─────┐     ┌─────▼─────┐
                │ PTY layer │     │ term view │
                │ observe   │     │ drawer    │
                │ attach    │     │ overlays  │
                └───────────┘     └───────────┘
```

## Plan 一覧

| # | ファイル | 概要 | 主な依存 | 出口 |
|---|---|---|---|---|
| 01 | [01-unified-input-router-and-mode-ui.md](plans/01-unified-input-router-and-mode-ui.md) | `agent-term` の入力面ルーティングと mode 表示、および boot profile 契約 | `rich-terminal`, `shell-and-init`, `agent-transport` | `term` / `agent-term` を kernel profile で切り替えられ、`agent-term` 側で shell と agent を同じ入力面から使える |
| 02 | [02-command-recovery-and-safe-corrections.md](plans/02-command-recovery-and-safe-corrections.md) | typo / command-not-found / console error 回復 | 01 | inline suggestion と安全な補正が成立する |
| 03 | [03-agent-session-surface-and-policy-controls.md](plans/03-agent-session-surface-and-policy-controls.md) | agent drawer、session 状態、permission / audit UI | 01, `agent-transport` | `agent-term` 上で agent session を継続運用できる |
| 04 | [04-agent-mediated-shell-actions.md](plans/04-agent-mediated-shell-actions.md) | agent 提案 command の approval / 実行 / 再投入 | 02, 03 | agent が shell action を安全に実行できる |
| 05 | [05-interactive-pty-observe-and-attach.md](plans/05-interactive-pty-observe-and-attach.md) | interactive CLI への observe-first attach | 03, 04 | running PTY を agent が読める |
| 06 | [06-vi-agent-native-commands.md](plans/06-vi-agent-native-commands.md) | `vi` の selection-aware agent command と diff apply | 03, 04 | `vi` から自然に agent を使える |
| 07 | [07-validation-rollout-and-guardrails.md](plans/07-validation-rollout-and-guardrails.md) | feature flag、性能監視、host/QEMU smoke、docs | 全体横断 | rollout 可能な品質と安全策が揃う |

## 実装順序

```text
M0: 入力統合
  01 unified router + mode badge

M1: 補正
  02 typo / command-not-found / safe correction

M2: 会話面と権限
  03 agent drawer + session/policy controls

M3: shell action
  04 agent mediated shell actions

M4: interactive attach
  05 PTY observe/attach

M5: editor 統合
  06 vi native commands

M6: hardening
  07 validation / rollout / guardrails
```

## 想定変更対象

### 既存

- `src/usr/command/term.c`
- `src/include/sys/syscalldef.h`
- `src/syscall.c`
- `src/usr/init.c`
- `src/usr/lib/libc/init_policy.c`
- `src/usr/command/agent.c`
- `src/usr/command/eshell.c`
- `src/usr/command/vi.c`
- `src/usr/lib/libc/shell_completion.c`
- `src/usr/lib/libc/shell_executor.c`
- `src/usr/lib/libc/vi_buffer.c`
- `src/usr/lib/libc/vi_screen.c`
- `src/usr/lib/libagent/repl.c`
- `src/usr/lib/libagent/agent_loop.c`
- `src/usr/lib/libagent/tool_dispatch.c`
- `src/usr/lib/libagent/permissions.c`
- `src/usr/lib/libagent/audit.c`
- `src/usr/lib/libagent/session.c`

### 新規候補

- `src/usr/include/term_agent_router.h`
- `src/usr/include/boot_profile.h`
- `src/usr/lib/libc/i386/get_boot_profile.S`
- `src/usr/lib/libc/term_agent_router.c`
- `src/usr/lib/libc/term_command_recovery.c`
- `src/usr/lib/libagent/term_session_surface.c`
- `src/usr/lib/libagent/pty_observer.c`
- `src/usr/lib/libc/vi_agent.c`
- `src/usr/command/agent-term.c`
- `src/usr/include/agent/pty_observer.h`
- `src/usr/include/vi_agent.h`
- `tests/test_term_agent_router.c`
- `tests/test_term_command_recovery.c`
- `tests/test_pty_observer.c`
- `tests/test_vi_agent.c`
- `src/test/run_qemu_agent_shell_fusion_smoke.py`

## 検証方針

- host unit test
  - boot profile
  - ルーティング
  - suggestion ranking
  - permission / approval state machine
  - selection export / diff apply
- QEMU smoke
  - `classic` / `agent` profile 切替
  - shell fast path
  - typo correction
  - agent suggestion -> approve -> shell 実行
  - PTY observe
  - `vi` の `:AgentEdit`
- 性能
  - drawer / overlay 有効時の redraw 回数
  - long output 時の scroll / resize 回帰

## TASKS.md

着手単位は `TASKS.md` で追跡する。
