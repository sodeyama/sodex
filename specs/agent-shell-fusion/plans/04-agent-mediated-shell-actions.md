# Plan 04: agent mediated shell actions

## 概要

drawer ができたら、次は agent が shell action を提案し、
承認の上で実行し、その結果を再び会話へ戻せるようにする。

ここで重要なのは、
agent が shell を直接支配するのではなく、
`term` が approval gate と execution record を握ることだ。

## 初期 scope

- agent 提案を executable command block として表示する
- `1回許可` / `session許可` / `deny` を扱う
- approved command を shell 経由で実行する
- bounded output を session context へ戻す
- long-running command の attach / detach を扱う

## 非ゴール

- 完全自律モードを既定にすること
- interactive PTY app への無確認入力
- shell script 全文を agent が直接書き換えること

## command block UI

例:

```text
assistant wants to run:
  grep -R "agent" specs
[run once] [allow session] [deny] [edit]
```

`edit` を入れる理由:

- 提案をそのまま実行したくない場合がある
- user が細部だけ直したい場合がある

MVP では 1 command block ずつ順に承認する。

## approval policy

承認単位は 2 軸に分ける。

- command class
  - read-only
  - write
  - process control
  - network
- scope
  - once
  - current session
  - deny

初期は command class を heuristics でよい。
精密な shell AST ベース分類は後段でもよい。

## 実行経路

1. agent が command proposal を返す
2. `term` drawer が approval を出す
3. user が選ぶ
4. `term` が shell 実行を起動する
5. stdout/stderr を bounded capture する
6. terminal へも出しつつ、session へも要約を戻す

## bounded output

`agent-transport` 側の bounded output を流用する。

会話へ戻す情報:

- command
- exit status
- stdout head/tail
- stderr head/tail
- artifact path

terminal 表示は全文を許しつつ、
agent 文脈へは要約だけ戻す。

## long-running command

初期対応:

- foreground run
- detach
- re-attach status 表示

例:

- build
- tail
- long test

drawer は command block ごとに `running` / `done` / `failed` を表示する。

## 実装方針

### 1. `run_command` をそのまま表に出しすぎない

backend の tool と terminal UX を直接同一視せず、
`term` 側で approval と visible record を追加する。

### 2. command class は conservative にする

誤って read-only 扱いすると危険なので、
分類不能な command は高リスク側へ倒す。

### 3. shell history と session history を分ける

shell としての履歴は既存 `history` に残す。
agent action としての履歴は session / audit に残す。

## 実装ステップ

1. command proposal block の描画を追加する
2. approval state machine を作る
3. shell 実行と bounded capture を接続する
4. session / audit へ戻す event を整える
5. long-running attach / detach を追加する

## 変更対象

- 既存
  - `src/usr/command/term.c`
  - `src/usr/command/agent.c`
  - `src/usr/lib/libagent/agent_loop.c`
  - `src/usr/lib/libagent/tool_dispatch.c`
  - `src/usr/lib/libagent/audit.c`
- 新規候補
  - `src/usr/lib/libagent/term_command_block.c`
  - `src/usr/include/agent/term_command_block.h`
  - `tests/test_term_command_block.c`

## 検証

- host
  - once/session/deny の遷移
  - high-risk command の deny 既定
  - bounded output truncation
- QEMU
  - `find` 提案 -> approve -> 出力が session に戻る
  - `echo hi > file` 提案 -> approval 必須
  - long-running command の detach / reattach

## 完了条件

- agent 提案 command を実行候補 block として表示できる
- approval を通した shell command だけ実行される
- 実行結果が terminal と session の両方に残る
- long-running command を扱える
