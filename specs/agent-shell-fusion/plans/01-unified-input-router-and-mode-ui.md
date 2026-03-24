# Plan 01: unified input router と mode UI

## 概要

最初に必要なのは、新しい agent を作ることではなく、
**既存 `term` を残したまま、新規 `agent-term` を boot で選べるようにし、**
その `agent-term` の 1 本の入力面を shell と agent の両方へ安全に振り分けることだ。

この Plan では

- kernel terminal profile 契約
- `init` による `term` / `agent-term` 切替
- `agent-term` の route layer

をまとめて定義する。

`agent-term` は `/usr/bin/term` の単純置換ではなく、
新しい userland terminal client として追加する。

この Plan では `agent-term` に route layer を追加し、
同じ入力欄で次の 3 mode を扱えるようにする。

- `auto`
- `shell`
- `agent`

ただし MVP では shell fast path を最優先にし、
既存 shell の決定性を壊さない。

## 初期 scope

- kernel terminal profile (`classic` / `agent`) 契約を追加する
- `init` が profile で `/usr/bin/term` と `/usr/bin/agent-term` を切り替える
- `agent-term` に route state を追加する
- line を shell / agent のどちらへ流すか判定する
- mode badge と route reason を表示する
- shell fast path の判定を LLM 非依存で行う
- agent force の導線を追加する

## 非ゴール

- typo correction
- command-not-found recovery
- agent conversation drawer
- approval / audit
- running PTY attach

これらは後続 Plan に分ける。

## boot profile

### profile 種別

- `classic`
  - `/usr/bin/term`
- `agent`
  - `/usr/bin/agent-term`

### 契約

kernel は boot 時に profile を 1 つ持つ。
`init` はそれを読んで respawn 対象を決める。

MVP では userland 側に次の helper がある前提にする。

```c
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

内部実装が build-time define でも syscall でも構わないが、
userland から見える契約は固定する。

### syscall 番号

MVP では次で固定する。

```c
#define SYS_CALL_GET_BOOT_PROFILE 124
```

既存の getter 系 syscall (`GET_FB_INFO`, `GET_KERNEL_TICK`) の続きに置く。

### 構造体

```c
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
```

MVP の `flags` は 0 固定とする。

### `@terminal` token

`/etc/inittab` は terminal respawn command を path 直書きではなく、
`@terminal` と書けるようにする。

```text
respawn:user:@terminal
```

解決規則:

- `@terminal` + `classic` -> `/usr/bin/term`
- `@terminal` + `agent` -> `/usr/bin/agent-term`
- profile 取得失敗 -> `/usr/bin/term`

path が直書きされている場合は token 解決しない。

### exec fallback

`init` の terminal 起動は次の順で行う。

1. resolved command
2. `resolved=/usr/bin/agent-term` かつ失敗時は `/usr/bin/term`
3. それも失敗したら rescue respawn

この fallback は `agent-term` 側ではなく `init` 側で持つ。
理由は、`agent-term` バイナリが壊れている場合でも退避したいため。

### build knob

build-time 既定は次に統一する。

```text
SODEX_TERM_PROFILE=classic|agent
```

用途:

- `src/makefile` の kernel CFLAGS
- `bin/start.sh`
- QEMU smoke

runlevel の既存 knob とは分ける。

## ルーティング方針

### 1. default は `auto`

`auto` では次の順に判定する。

1. line-local agent force
2. shell fast path
3. ambiguous fallback

### 2. shell fast path

shell fast path に入る条件は次を満たす line とする。

- shell parser が受理する
- command position の先頭 token が
  - builtin
  - alias
  - external command
  - path 指定 command
  のどれかへ解決できる

これに該当する line は LLM を通さず shell 実行へ進める。

### 3. agent force

MVP では line 先頭の `@` を agent force とする。

例:

```text
@ このディレクトリの役割を整理して
@ 直前のビルド失敗を要約して
```

理由:

- `!` は shell history 展開と衝突する
- shell fast path が既定なので shell force は MVP では不要
- 1 文字で自然言語入力へ寄せやすい

### 4. ambiguous fallback

shell parse は通るが command 解決に失敗する line は、
すぐ agent へ流さず、次のどちらかに送る。

- Plan 02 の typo / recovery
- 「agent に送るか」を確認する fallback

この Plan ではまだ fallback 実行までは持たず、
route reason の記録までに留める。

## UI

### mode badge

prompt 近傍に次を出す。

```text
[auto]
[shell]
[agent]
```

`auto` では最終的に採用された route reason を 1 行だけ出す。

例:

```text
[auto:shell builtin]
[auto:shell path]
[auto:agent forced]
[auto:unknown command]
```

### line-local 状態

`@` で agent force された line は、submit 前に badge 上も `agent` に切り替える。
送信後は元の default mode へ戻す。

### key 操作

MVP では mode toggle の常駐 hotkey は必須にしない。
まずは text prefix + badge で成立させる。
常駐 toggle は後段で追加してよい。

## 設計メモ

### `agent-term` と shell の責務分離

- `agent-term`
  - line buffering
  - route 判定
  - mode 表示
- shell
  - parse
  - expansion
  - execute
- agent
  - conversation
  - session
  - tools

route 判定のために shell の lookup は使うが、
shell 実行の詳細までは `agent-term` 側へ持ち込まない。

### route 判定 API

新規 helper 例:

```c
enum term_route_kind {
  TERM_ROUTE_SHELL,
  TERM_ROUTE_AGENT,
  TERM_ROUTE_RECOVERY,
  TERM_ROUTE_CONFIRM
};

struct term_route_decision {
  enum term_route_kind kind;
  char reason[64];
};
```

入力 1 行に対して判定のみ返す pure logic を先に作り、
`term.c` はその結果で UI と dispatch を決める形に寄せる。

## 実装ステップ

1. route state と decision struct を定義する
2. `SYS_CALL_GET_BOOT_PROFILE` と userland wrapper を追加する
3. `init_policy` に `@terminal` token を追加する
4. `init` が profile に応じて起動 command を選び、fallback を持つようにする
5. shell lookup を使う route 判定 helper を作る
6. `@` force と shell fast path を実装する
7. `agent-term` の prompt/badge 表示を追加する
8. host/QEMU test を追加する

## 変更対象

- 既存
  - `src/include/sys/syscalldef.h`
  - `src/syscall.c`
  - `src/usr/init.c`
  - `src/usr/lib/libc/init_policy.c`
  - `src/usr/command/term.c`
  - `src/usr/lib/libc/shell_executor.c`
  - `src/usr/lib/libc/shell_completion.c`
  - `src/usr/include/shell.h`
- 新規候補
  - `src/usr/command/agent-term.c`
  - `src/usr/include/boot_profile.h`
  - `src/usr/lib/libc/i386/get_boot_profile.S`
  - `src/usr/include/term_agent_router.h`
  - `src/usr/lib/libc/term_agent_router.c`
  - `tests/test_term_agent_router.c`

## 検証

- host unit test
  - `@terminal` token 解決
  - profile 取得失敗 -> classic fallback
  - `agent-term` spawn 失敗 -> `term` fallback
  - boot profile -> spawn command 解決
  - `ls` -> shell
  - `grep foo file` -> shell
  - `./script.sh` -> shell
  - `@ repo を説明して` -> agent
  - `foobarbaz` -> recovery / confirm
- QEMU smoke
  - `classic` profile で従来 `/usr/bin/term` が起動する
  - `agent` profile で `/usr/bin/agent-term` が起動する
  - `agent-term` 上で `pwd` と `@ pwd を説明して` が別経路で処理される

## 完了条件

- kernel profile で `term` / `agent-term` を切り替えられる
- `agent-term` の入力 1 本で shell / agent の route を判断できる
- shell fast path は LLM を経由しない
- agent force がテキスト入力だけで使える
- route reason が UI で見える
