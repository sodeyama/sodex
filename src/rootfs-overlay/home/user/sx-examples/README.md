# sx examples

`/home/user/sx-examples/` は、`sxi` を guest 内で試すためのサンプル集です。
構文の基本だけでなく、`stdin`、file I/O、`argv`、`spawn`、pipe、`fork` までまとめて置いています。

## 最初の実行

```sh
cd /home/user/sx-examples
sxi --check hello.sx
sxi hello.sx
```

## 基本文法

### 1. hello

```sh
sxi hello.sx
```

関数呼び出しと文字列連結の最小例です。

### 2. 演算子

```sh
sxi operators.sx
```

`+ - * / %`、比較、`&&`、`||`、`!`、grouping を確認できます。

### 3. `while`

```sh
sxi while_counter.sx
```

代入を伴う最小の loop 例です。

### 4. `for`

```sh
sxi for_counter.sx
```

`for (init; condition; step)` の基本形です。

### 5. `break` / `continue`

```sh
sxi break_continue.sx
```

loop 制御と分岐を組み合わせた例です。

### 6. scope

```sh
sxi scope_blocks.sx
```

block ごとの scope と shadowing を確認できます。

### 7. 再帰

```sh
sxi recursive_sum.sx
```

user function の再帰呼び出し例です。

### 8. `import`

```sh
sxi import_main.sx
```

相対 `import` と module 関数呼び出しを試せます。

## builtin namespace

### 9. stdlib `import`

```sh
sxi stdlib_import.sx
```

`/usr/lib/sx` 配下の module を import する例です。

### 10. JSON

```sh
sxi json_report.sx
```

`json.get_str`、`json.get_bool`、`json.get_i32`、`json.valid` の例です。

### 11. file I/O

```sh
sxi copy_file.sx
cat /tmp/sx-copy.txt
```

`fs.read_text`、`fs.write_text`、`fs.append_text`、`fs.exists` を使います。

### 12. process capture

```sh
sxi proc_capture.sx
```

`proc.capture`、`proc.run`、`proc.status_ok` の例です。

### 13. stdin

```sh
sxi stdin_echo.sx < stdin_source.txt
```

`io.read_line` と `io.read_all` を使います。

### 14. `argv` / path / time

```sh
sxi argv_fs_time.sx alpha beta
```

`proc.argv_count`、`proc.argv`、`fs.mkdir`、`fs.list_dir`、`fs.chdir`、`fs.cwd`、`fs.rename`、`fs.remove`、`time.now_ticks`、`time.sleep_ticks` をまとめて確認できます。

### 15. `spawn` / `wait`

```sh
sxi spawn_wait.sx
```

`proc.spawn` と `proc.wait` で child process を待ち、exit code を読む例です。
副作用として一時ファイルも作り、終了コードとファイル内容の両方を確認します。

### 16. pipe / fd I/O

```sh
sxi pipe_roundtrip.sx
```

`proc.pipe`、`proc.spawn_io`、`proc.pipe_read_fd`、`proc.pipe_write_fd`、`proc.pipe_close`、`io.read_fd`、`io.write_fd`、`io.close` を使います。

### 17. `fork`

```sh
sxi fork_wait.sx
```

`proc.fork`、`proc.wait`、`proc.exit` を最小構成で試せます。

### 18. env / bytes / result

```sh
sxi env_bytes_result.sx
```

`proc.has_env`、`bytes.from_text`、`bytes.to_text`、`bytes.len`、
`fs.read_bytes`、`fs.write_bytes`、`fs.try_read_text`、`result.ok`、
`result.err`、`result.is_ok`、`result.value`、`result.error`、
`proc.try_capture` をまとめて確認できます。

### 19. list / map

```sh
sxi list_map.sx
```

`list.new`、`list.push`、`list.get`、`list.set`、`list.len`、
`map.new`、`map.set`、`map.get`、`map.remove`、`map.len`、
`map.has` を使います。

## ファイル一覧

- `hello.sx`
- `operators.sx`
- `while_counter.sx`
- `for_counter.sx`
- `break_continue.sx`
- `scope_blocks.sx`
- `recursive_sum.sx`
- `import_main.sx`
- `import_lib.sx`
- `stdlib_import.sx`
- `json_report.sx`
- `copy_file.sx`
- `proc_capture.sx`
- `stdin_echo.sx`
- `argv_fs_time.sx`
- `spawn_wait.sx`
- `pipe_roundtrip.sx`
- `fork_wait.sx`
- `env_bytes_result.sx`
- `list_map.sx`
- `copy_source.txt`
- `stdin_source.txt`

## メモ

- comment は `//`
- string は `\"`、`\\n`、`\\t`、`\\\\` を使えます
- path は文字列で渡します
- 失敗時は診断と stack trace を表示します
