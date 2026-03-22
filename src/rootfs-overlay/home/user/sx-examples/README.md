# sx examples

`/home/user/sx-examples/` は、`sxi` を guest 内で試すための最小サンプル集です。

## 使い方

```sh
cd /home/user/sx-examples
sxi --check hello.sx
sxi hello.sx
```

`import_main.sx` は相対 import の例です。

```sh
sxi import_main.sx
```

`copy_file.sx` は `/tmp/sx-copy.txt` を作ります。

```sh
sxi copy_file.sx
cat /tmp/sx-copy.txt
```

## ファイル一覧

- `hello.sx`
  - `fn`、`let`、`text.trim`、`text.concat`、`io.println`
- `import_main.sx`
  - `import` と関数呼び出し
- `import_lib.sx`
  - `import_main.sx` から読む module
- `json_report.sx`
  - `json.get_*` と `json.valid`
- `copy_file.sx`
  - `fs.read_text`、`fs.write_text`、`fs.append_text`、`fs.exists`
- `proc_capture.sx`
  - `proc.capture`、`proc.run`、`proc.status_ok`
- `copy_source.txt`
  - `copy_file.sx` の入力ファイル

## メモ

- comment は `//`
- string は `\"`、`\\n`、`\\t`、`\\\\` を使えます
- path は今の `sxi` と同じく文字列で渡します
- 失敗時は診断と stack trace を表示します
