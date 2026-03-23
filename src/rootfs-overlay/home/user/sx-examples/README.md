# sx examples

`/home/user/sx-examples/` は、`sxi` を guest 内で試すためのサンプル集です。
構文の基本だけでなく、literal、`stdin`、grep-lite、file I/O、`argv`、`spawn`、pipe、`fork`、network client/server、最小 `httpd`、静的 HTML `httpd` までまとめて置いています。
文法と構文規則は `LANGUAGE.md` を先に見てください。

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

### 14. grep-lite

```sh
sxi grep_lite.sx alpha < grep_source.txt
```

`io.read_line`、`text.contains`、`while` を組み合わせた最小の filter 例です。
v0 では `io.read_line()` が EOF と空行の両方で `""` を返すので、この sample は空行を含まない入力を前提にしています。

### 15. `argv` / path / time

```sh
sxi argv_fs_time.sx alpha beta
```

`proc.argv_count`、`proc.argv`、`fs.mkdir`、`fs.list_dir`、`fs.chdir`、`fs.cwd`、`fs.rename`、`fs.remove`、`time.now_ticks`、`time.sleep_ticks` をまとめて確認できます。

### 16. `spawn` / `wait`

```sh
sxi spawn_wait.sx
```

`proc.spawn` と `proc.wait` で child process を待ち、exit code を読む例です。
副作用として一時ファイルも作り、終了コードとファイル内容の両方を確認します。

### 17. pipe / fd I/O

```sh
sxi pipe_roundtrip.sx
```

`proc.pipe`、`proc.spawn_io`、`proc.pipe_read_fd`、`proc.pipe_write_fd`、`proc.pipe_close`、`io.read_fd`、`io.write_fd`、`io.close` を使います。

### 18. `fork`

```sh
sxi fork_wait.sx
```

`proc.fork`、`proc.wait`、`proc.exit` を最小構成で試せます。

### 19. env / bytes / result

```sh
sxi env_bytes_result.sx
```

`proc.has_env`、`bytes.from_text`、`bytes.to_text`、`bytes.len`、
`fs.read_bytes`、`fs.write_bytes`、`fs.try_read_text`、`result.ok`、
`result.err`、`result.is_ok`、`result.value`、`result.error`、
`proc.try_capture` をまとめて確認できます。

### 20. list / map

```sh
sxi list_map.sx
```

`[]`、`{}`、`list.push`、`list.get`、`list.set`、`list.len`、
`map.get`、`map.remove`、`map.len`、`map.has` を使います。

### 21. literal / `else if`

```sh
sxi literal_branching.sx
```

list / map literal と `else if` の最小例です。

### 22. network client

```sh
sxi net_client.sx
```

`net.connect`、`net.write`、`net.poll_read`、`net.read`、`net.close` の例です。
QEMU smoke では host 側 server へ接続します。

### 23. network server

```sh
sxi net_server.sx
```

`net.listen`、`net.accept`、`net.read`、`net.write`、`net.close` の例です。
QEMU smoke では host 側 client が接続します。

### 24. minimal `httpd`

```sh
sxi httpd.sx 18083 3
```

`text.index_of`、`text.slice`、`text.to_i32` と `net.listen` / `net.accept` を使って
最小の HTTP server を書く例です。
第 1 引数で port、第 2 引数で処理する request 数を指定できます。
既定値は `18083` と無限ループです。

### 25. static HTML `httpd`

```sh
sxi static_httpd.sx 18085
```

`/home/user/www/index.html` を `GET /` と `GET /index.html` で返す最小の static server です。
第 1 引数で port、第 2 引数で処理する request 数を指定できます。
既定値は `18085` と `32` です。
`bin/start.sh` で起動していれば、host の browser からそのまま `http://127.0.0.1:18085/` を開けます。
別経路で guest を起動していて `18085` を直接 forward していないときは、SSH local forward でも見られます。

```sh
ssh -N -L 18085:127.0.0.1:18085 -p 10022 -o PubkeyAuthentication=no root@127.0.0.1
```

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
- `grep_lite.sx`
- `argv_fs_time.sx`
- `spawn_wait.sx`
- `pipe_roundtrip.sx`
- `fork_wait.sx`
- `env_bytes_result.sx`
- `list_map.sx`
- `literal_branching.sx`
- `net_client.sx`
- `net_server.sx`
- `httpd.sx`
- `static_httpd.sx`
- `LANGUAGE.md`
- `copy_source.txt`
- `stdin_source.txt`
- `grep_source.txt`

`static_httpd.sx` が返す page 本体は `/home/user/www/index.html` に置いてあります。

## メモ

- comment は `//`
- string は `\"`、`\\n`、`\\r`、`\\t`、`\\\\` を使えます
- path は文字列で渡します
- 失敗時は診断と stack trace を表示します
