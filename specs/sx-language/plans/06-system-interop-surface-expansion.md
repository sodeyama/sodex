# Plan 06: System Interop Surface の拡張

## 目的

`sx` を guest 内の実用 script 言語として押し上げるために、
`argv`、env、fd / bytes I/O、path、time、`spawn` / `wait` / `pipe` / `fork`、
`list` / `map` / `result` を含む system interop surface を固定する。

## 背景

現状の `sx` は `io.print*`、`fs.read_text`、`proc.run`、`proc.capture` までで、
「簡単な file 変換」には足りる一方で、一般的な interpreter に期待される
次の操作が弱い。

- script 引数を扱う
- stdin / stdout / pipe を経由して command をつなぐ
- cwd を移動し、relative path 前提で script を組む
- child process を待つ
- 必要最小限の `fork` で親子分岐を書く

Sodex 自体は `open/read/write/close/chdir/pipe/waitpid/execve` を持ち、
kernel 側に `fork` syscall 番号もあるため、language surface を先に整理して
runtime 実装へ落とし込む価値がある。

## 今回の scope

### 1. `io`

- `io.read_all() -> str`
- `io.read_line() -> str`
- `io.read_fd(fd) -> str`
- `io.read_fd_bytes(fd) -> bytes`
- `io.write_fd(fd, text) -> i32`
- `io.write_fd_bytes(fd, bytes) -> i32`
- `io.close(fd) -> bool`

`print` / `println` はそのまま残す。

### 2. `fs`

- `fs.mkdir(path) -> bool`
- `fs.remove(path) -> bool`
- `fs.rename(from, to) -> bool`
- `fs.chdir(path) -> bool`
- `fs.cwd() -> str`
- `fs.is_dir(path) -> bool`
- `fs.read_bytes(path) -> bytes`
- `fs.write_bytes(path, data) -> bool`
- `fs.try_read_text(path) -> result`
- `fs.try_read_bytes(path) -> result`

既存の `exists` / `read_text` / `write_text` / `append_text` / `list_dir` と合わせて、
path ベース script の最小系をそろえる。

### 3. `time`

- `time.now_ticks() -> i32`
- `time.sleep_ticks(ticks) -> bool`

wall clock ではなく kernel tick を expose する。

### 4. `proc`

- `proc.argv_count() -> i32`
- `proc.argv(index) -> str`
- `proc.env(name) -> str`
- `proc.has_env(name) -> bool`
- `proc.spawn(path, ...args) -> i32`
- `proc.wait(pid) -> i32`
- `proc.pipe() -> i32`
- `proc.pipe_read_fd(handle) -> i32`
- `proc.pipe_write_fd(handle) -> i32`
- `proc.pipe_close(handle) -> bool`
- `proc.spawn_io(path, stdin_fd, stdout_fd, stderr_fd, ...args) -> i32`
- `proc.fork() -> i32`
- `proc.exit(code) -> unit`
- `proc.try_run(path, ...args) -> result`
- `proc.try_capture(path, ...args) -> result`

`proc.run` / `proc.capture` / `proc.status_ok` は残し、
`spawn` / `wait` / `pipe` を lower-level API として追加する。

### 5. helper object

- `bytes.from_text(text) -> bytes`
- `bytes.to_text(data) -> str`
- `bytes.len(data) -> i32`
- `list.new() -> list`
- `list.push(list, value) -> i32`
- `list.get(list, index) -> any`
- `list.set(list, index, value) -> bool`
- `list.len(list) -> i32`
- `map.new() -> map`
- `map.set(map, key, value) -> bool`
- `map.get(map, key) -> any`
- `map.has(map, key) -> bool`
- `map.remove(map, key) -> bool`
- `map.len(map) -> i32`
- `result.ok(value) -> result`
- `result.err(message) -> result`
- `result.is_ok(result) -> bool`
- `result.value(result) -> any`
- `result.error(result) -> str`

## 契約方針

### 1. 値の形

- pid / fd / pipe handle は `i32`
- path / argv / captured text は `str`
- 成否だけを返す操作は `bool`

複合値を返さず、pipe は handle + accessor に分ける。

### 2. `fork`

- parent では child pid を返す
- child では `0` を返す
- 失敗は runtime error

`fork` は raw API として expose するが、通常の command 連携は
`spawn` / `wait` / `pipe` を優先する。

### 3. check mode

`sxi --check` では side effect は起こさない。
ただし型と制御の検査を通すため、system builtin は dummy 値を返す。
このため `fork` や `spawn` を条件分岐に使う script では、
`--check` が片側 branch しか踏まない場合がある。

## 非ゴール

- socket / network API
- poll / select / 非同期 I/O
- pipe を tuple で返す多値戻り値

## 実装ステップ

1. `sx` language 側で builtin 名、引数、戻り値、check mode 契約を固定する
2. `sxi` runtime 側で fd / pid / pipe handle model を追加する
3. host test と QEMU smoke に `argv` / env / path / pipe / fork / bytes / collection の回帰を足す
4. guest sample を `/home/user/sx-examples/` に増やす

## 変更対象

- `specs/sx-language/README.md`
- `specs/sx-language/TASKS.md`
- `specs/sxi-runtime/README.md`
- `specs/sxi-runtime/TASKS.md`
- `src/usr/include/sx_runtime.h`
- `src/usr/lib/libsx/runtime.c`
- `src/usr/command/sxi.c`
- `tests/test_sxi_runtime.c`
- `tests/test_sxi_cli.c`
- `src/test/run_qemu_sxi_smoke.py`
- `src/rootfs-overlay/home/user/sx-examples/`
