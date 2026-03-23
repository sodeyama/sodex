# Plan 05: Process / IO / Fork Expansion

## 目的

`sxi` runtime に `argv`、env、fd / bytes I/O、path、time、
`spawn` / `wait` / `pipe` / `fork`、`list` / `map` / `result`
を実装し、host test と QEMU smoke で固定する。

## 背景

既存の `sxi` は `proc.run` と `proc.capture` を持つが、
runtime 内部は「1 回 command を起動して待つ」前提が強い。
これでは shell を使わずに child process を配線する script が書きづらい。

また Sodex kernel の `fork()` は最小 clone で実装済みだが、
userland から安定して使うには file table、cwd、page clone、wait の経路を
QEMU smoke で継続的に固める必要がある。

## 実装方針

### 1. runtime state

`struct sx_runtime` に次を追加する。

- script argv の固定長 table
- pipe handle table
- list / map / result handle table
- fd close 時に handle table を同期するための補助

GC は入れず、固定長 table で先に成立させる。

### 2. host build

`TEST_BUILD` では host libc の `fork()` + `execv()` + `waitpid()` を使い、
guest の spawn 的 `execve()` を近似する。

- `spawn` は host `fork` 後に child `execv`
- `spawn_io` は child 側で `dup2`
- `capture` も同じ helper に寄せる

### 3. guest build

guest では既存の spawn 的 `execve()` を活かす。

- `spawn` は `execve()` が返す child pid をそのまま返す
- `spawn_io` は親の stdio fd を一時的に差し替えて `execve()` し、直後に復元する
- `wait` は `waitpid()`
- `pipe` は既存 syscall を使う

### 4. kernel fork

`fork()` は次の最小 clone で実装する。

- parent task metadata の複製
- file table の refcount 付き clone
- current working directory の継承
- user page table / user physical page の複製
- child context を「syscall return 直後」へ向けて構築し、`eax=0` にする

copy-on-write は入れず、full copy で始める。

## テスト方針

### host unit test

- `proc.argv_count` / `proc.argv`
- `fs.cwd` / `fs.chdir` / `fs.mkdir` / `fs.rename` / `fs.remove`
- `time.now_ticks` / `time.sleep_ticks`
- `proc.has_env`
- `proc.spawn` / `proc.wait`
- `proc.pipe` + `io.write_fd` + `io.read_fd`
- `fs.read_bytes` / `fs.write_bytes`
- `list` / `map` / `result`
- `proc.fork` + `proc.exit`

### QEMU smoke

- file mode で argv を受ける script
- cwd / mkdir / rename / remove を使う script
- pipe で child stdout を読む script
- `fork` で child が file を作り、parent が wait する script
- bytes / result / collection を触る script

### sample

`/home/user/sx-examples/` に次を置く。

- argv
- cwd / path
- spawn
- spawn + pipe
- fork
- time
- env / bytes / result
- list / map

## リスク

### 1. kernel fork の破損

page clone や child context が崩れると page fault になりやすい。
このため host 先行で surface を固め、guest は QEMU smoke を細かく刻む。

### 2. fd restore の漏れ

`spawn_io` は親 fd を一時差し替えるため、restore 漏れがあると
以後の REPL / command 出力が壊れる。
restore helper を 1 箇所へ寄せる。

### 3. check mode の限界

`--check` は side effect を起こさないため、
`fork` / `spawn` 分岐の全 path を静的には踏めない。
実行回帰は host/QEMU smoke に寄せる。

## 実装ステップ

1. spec 更新と `sx_runtime` の state 追加
2. host/guest 共通 helper で fd / process 実装を整理
3. `sxi` CLI に script argv を通す
4. kernel `fork()` を最小 clone で実装する
5. host test、QEMU smoke、sample を拡充する
