# Plan 03: agent session surface と policy control

## 概要

router と correction だけでは、
agent が terminal に「いる」感覚は出ない。

本 Plan では `term` 内に agent session の surface を追加し、
現在の session、permission mode、approval、recent context を
同じ terminal で扱えるようにする。

MVP は side pane ではなく、下部 drawer 方式にする。
理由は、現在の `term` 実装と redraw への影響を最小化しやすいため。

## 初期 scope

- `term` 下部に agent drawer を追加する
- transcript の短い履歴を表示する
- session id / cwd / context usage / permission mode を表示する
- slash command を `term` 内から扱えるようにする
- recent command block を agent 文脈へ橋渡しする
- approval prompt と audit 要約を表示する

## 非ゴール

- shell action の実行自体
- PTY attach
- `vi` 連携

## UI 設計

### drawer レイアウト

```text
┌ terminal viewport                         ┐
│                                           │
│ ...                                       │
├ agent drawer status ----------------------┤
│ session=main cwd=/home/user ctx=41%       │
│ perm=standard route=agent recent=ls -l    │
│ user: この repo の構成を説明して           │
│ assistant: まず src/usr を見ます           │
└ input line -------------------------------┘
```

初期高さは 6-8 行で固定し、
必要なら後段で expand / collapse を入れる。

### drawer 状態

- hidden
- transient
  - agent route 時だけ表示
- pinned
  - 常時表示

MVP は `transient` と `pinned` の 2 状態でよい。

### slash command

drawer active 中だけ、`/` 開始行を agent slash command として扱う。

初期対象:

- `/help`
- `/clear`
- `/compact`
- `/permissions`
- `/sessions`
- `/resume`
- `/status`

## session 表示

既存 `session.c`, `repl.c`, `permissions.c`, `audit.c` を再利用する。

drawer で見せる最低情報:

- session 名または短縮 id
- `cwd`
- model
- context usage
- permission mode
- 直前 route

## recent command block

agent が「直前の shell 実行」を自然に参照できるよう、
`term` は recent command block をリング保持する。

block 例:

- 入力 command
- exit status
- stdout tail
- stderr tail
- 実行時刻

drawer から agent へ送るのは全文ではなく、
直近数 block と bounded output だけにする。

## approval / audit

approval は drawer 下部に 1 行で出す。

例:

```text
allow run_command? [once] [session] [deny]
```

audit は full log ではなく、
直近イベントの短い summary を出す。

例:

```text
audit: run_command allowed once
audit: write_file denied by policy
```

## 実装方針

### 1. agent backend を再実装しない

既存 `agent` REPL のロジックを drawer から呼び出す形にする。
session、permissions、compact は backend 側を再利用する。

### 2. drawer は alternate app ではなく `term` overlay として作る

別 PTY や別 terminal client を立てず、
既存 `term` の viewport 下部へ overlay する。

### 3. command block は transcript そのものと分ける

会話本文と shell artifact を分離し、
agent へ送るときだけ recent block を構造化注入する。

## 実装ステップ

1. drawer state と rendering を追加する
2. session status 表示を追加する
3. slash command の dispatcher を接続する
4. recent command block capture を実装する
5. approval / audit 行を追加する

## 変更対象

- 既存
  - `src/usr/command/term.c`
  - `src/usr/command/agent.c`
  - `src/usr/lib/libagent/repl.c`
  - `src/usr/lib/libagent/session.c`
  - `src/usr/lib/libagent/permissions.c`
  - `src/usr/lib/libagent/audit.c`
- 新規候補
  - `src/usr/lib/libagent/term_session_surface.c`
  - `src/usr/include/agent/term_session_surface.h`
  - `tests/test_term_session_surface.c`

## 検証

- host
  - drawer open/close/pin
  - session status serialization
  - recent command block truncation
  - approval state transitions
- QEMU
  - `@` で drawer が開く
  - `/compact` と `/permissions` が terminal 内で動く
  - 直前 command の stderr tail を session context に取り込める

## 完了条件

- `term` 内で agent session の transcript と状態を見られる
- REPL 相当の基本操作を drawer 経由で行える
- recent shell block を agent context として再利用できる
- permission / audit の短い UI が成立する
