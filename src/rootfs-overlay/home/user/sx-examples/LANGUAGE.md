# sx / sxi Language Reference

`/home/user/sx-examples/LANGUAGE.md` は、`sx` の文法と `sxi` の使い方をまとめた最小リファレンスです。
実例は同じ directory の `README.md` と `*.sx` を参照してください。

## `sxi` の使い方

```sh
sxi file.sx [args...]
sxi --check file.sx [args...]
sxi -e 'io.println("ok");'
sxi
```

- `file.sx`: file を読み込んで実行します
- `--check`: parse / import / name 解決 / 制御の妥当性だけ確認します
- `-e`: その場の短い code を実行します
- 引数は script 側で `proc.argv_count()` と `proc.argv(i)` から読めます
- REPL では `:load path`、`:reset`、`:quit` が使えます

## source rule

- source は UTF-8 text を前提にします
- identifier は ASCII のみです
- comment は `//` です
- statement の末尾は `;` が必要です
- block は `{ ... }` で書きます
- `if` / `while` / `for` の条件には `(` `)` が必要です
- list literal は `[` `]`、map literal は `{` `}` を使います
- string escape は `\"`、`\\n`、`\\t`、`\\\\` です
- integer literal は 10 進 `i32` です
- `true` と `false` が bool literal です

## `import`

`import` は `sxi` の source loader が処理する 1 行 directive です。

```sx
import "import_lib.sx";
import "std/strings";
```

- 行の形は `import "path";` です
- 同じ行の末尾には `// comment` を書けます
- absolute path、relative path、plain path を使えます
- plain path は、まず current file の directory、見つからなければ stdlib を探します
- import cycle は拒否されます

## top-level grammar

```text
program        = { import_line | function_decl | statement }
function_decl  = "fn" ident "(" [ ident { "," ident } ] ")" [ "->" ident ] block
statement      = let_stmt
               | assign_stmt
               | call_stmt
               | if_stmt
               | while_stmt
               | for_stmt
               | return_stmt
               | break_stmt
               | continue_stmt
               | block
```

### 例

```sx
fn greet(name) -> str {
  return "hello, " + name;
}

let user = "sodex";
io.println(greet(user));
```

## statement rule

### 変数

```sx
let name = "sx";
name = "sxi";
```

- `let` は現在の scope に束縛を追加します
- assignment は既存の名前を更新します

### 条件分岐

```sx
if (flag) {
  io.println("on");
} else if (other) {
  io.println("mid");
} else {
  io.println("off");
}
```

### loop

```sx
while (i < 3) {
  i = i + 1;
}

for (let j = 0; j < 5; j = j + 1) {
  if (j == 1) {
    continue;
  }
  if (j == 4) {
    break;
  }
}
```

- `for` header の `init` は `let` / assignment / call / empty
- `condition` は expr / empty
- `step` は assignment / call / empty
- `return`、`break`、`continue` は必ず `;` で閉じます

## expression rule

### primary

- string
- integer
- `true` / `false`
- identifier
- function call: `name(...)`
- namespace call: `io.println(...)`
- list literal: `[expr, expr]`
- map literal: `{"key": expr}`
- grouping: `(expr)`

### precedence

上から弱い順です。

```text
||
&&
== !=
< <= > >=
+ -
* / %
unary ! -
call / atom
```

### 例

```sx
let ok = (a + 1) * 2 >= 10 && !done;
```

## function と call

```sx
fn sum_to(n) -> i32 {
  if (n == 0) {
    return 0;
  }
  return n + sum_to(n - 1);
}
```

- parameter に型注釈はありません
- `-> type` は書けます
- v0 では return type label は主に宣言用です
- bare expression statement はなく、statement にできるのは call です

## namespace call

dot syntax は object field access ではなく、namespace builtin call だけです。

```sx
io.println("hello");
fs.write_text("/tmp/a.txt", "A");
proc.spawn("/usr/bin/cat", "/tmp/a.txt");
```

主な namespace:

- `io`
- `fs`
- `proc`
- `net`
- `json`
- `text`
- `time`
- `bytes`
- `list`
- `map`
- `result`
- `test`

## v0 でよく使う builtin

- text / console: `io.print`, `io.println`, `io.read_line`, `io.read_all`
- file: `fs.read_text`, `fs.write_text`, `fs.append_text`, `fs.read_bytes`, `fs.write_bytes`
- path: `fs.exists`, `fs.mkdir`, `fs.remove`, `fs.rename`, `fs.chdir`, `fs.cwd`, `fs.list_dir`, `fs.is_dir`
- process: `proc.run`, `proc.capture`, `proc.spawn`, `proc.spawn_io`, `proc.wait`, `proc.pipe`, `proc.fork`, `proc.exit`
- process helper: `proc.argv_count`, `proc.argv`, `proc.env`, `proc.has_env`, `proc.status_ok`, `proc.try_run`, `proc.try_capture`
- network: `net.connect`, `net.listen`, `net.accept`, `net.read`, `net.read_bytes`, `net.write`, `net.write_bytes`, `net.poll_read`, `net.close`
- JSON: `json.get_str`, `json.get_bool`, `json.get_i32`, `json.valid`
- bytes / collection: `bytes.*`, `list.*`, `map.*`, `result.*`

実例は `/home/user/sx-examples/README.md` と各 sample を見てください。

## scope と module

- block ごとに scope を持ちます
- 内側の `let` は outer binding を shadow できます
- imported file の top-level function / statement は source tree に取り込まれてから評価されます
- stdlib import は `import "std/strings";` のように書けます

## まだ無いもの

- lambda / closure
- class / method / property access
- shell 的 interpolation
- float / i64
- exception / `try` / `catch`

## 推奨の読み順

1. `/home/user/sx-examples/README.md`
2. `/home/user/sx-examples/hello.sx`
3. `/home/user/sx-examples/operators.sx`
4. `/home/user/sx-examples/argv_fs_time.sx`
5. `/home/user/sx-examples/env_bytes_result.sx`
6. `/home/user/sx-examples/list_map.sx`
7. `/home/user/sx-examples/literal_branching.sx`
8. `/home/user/sx-examples/net_client.sx`
9. `/home/user/sx-examples/net_server.sx`
