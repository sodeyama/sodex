# Plan 12: ツール実行エンジン

## 概要

Claude API からの `tool_use` レスポンスに対して、ユーザランド上でツールを実行し、
`tool_result` を構築して会話に返送するエンジンを実装する。
Claude Agent SDK の「ビルトインツール」に相当する機能を、Sodex のサブシステムに
マッピングする形で構築する。

## 目標

- Claude が返す `tool_use` ブロックからツール名と入力パラメータを取得し、対応するハンドラを呼び出せる
- ツールハンドラの結果を `tool_result` JSON に変換して Claude API に返送できる
- ツールレジストリにより、ハンドラの追加・削除が容易にできる
- エラー時は `is_error: true` 付きの tool_result を返し、Claude にリカバリを委ねる
- ツール実行のタイムアウトを設ける

## 背景

Plan 10 で `claude_build_tool_result()` と `claude_needs_tool_call()` は定義済み。
Plan 11 では tool_use ブロックのパースまで確認済み。
本 Plan では、パースされた tool_use を**実際に実行する**エンジンを構築する。

Claude Agent SDK のビルトインツール（Read, Write, Bash, Glob, Grep 等）は
ホスト OS のファイルシステムやシェルを操作するもの。Sodex では同等の操作を
ext3fs、プロセス管理、カーネル情報取得で代替する。

## 設計

### ツールレジストリ

```c
/* src/usr/include/agent/tool_registry.h */

#define TOOL_MAX_NAME         64
#define TOOL_MAX_DESCRIPTION 256
#define TOOL_MAX_RESULT     8192
#define TOOL_MAX_REGISTERED   16

/* ツール実行結果 */
struct tool_result {
    char content[TOOL_MAX_RESULT];
    int  content_len;
    int  is_error;
};

/* ツールハンドラの関数型 */
typedef int (*tool_handler_fn)(
    const char *input_json,   /* tool_use.input_json */
    int input_json_len,
    struct tool_result *result
);

/* ツール登録エントリ */
struct tool_entry {
    char name[TOOL_MAX_NAME];
    char description[TOOL_MAX_DESCRIPTION];
    const char *input_schema_json;  /* JSON Schema 文字列（コンパイル時定数） */
    tool_handler_fn handler;
};

/* レジストリ API */
void tool_registry_init(void);
int  tool_registry_add(const struct tool_entry *entry);
const struct tool_entry *tool_registry_find(const char *name);
int  tool_registry_count(void);
const struct tool_entry *tool_registry_get(int index);  /* ツール定義列挙用 */
```

### ツールディスパッチャ

```c
/* src/usr/include/agent/tool_dispatch.h */

/* tool_use ブロックを受け取り、対応ハンドラを実行して tool_result を返す */
int tool_dispatch(
    const struct claude_tool_use *tool_use,
    struct tool_result *result
);

/* 複数の tool_use ブロックを順次実行 */
int tool_dispatch_all(
    const struct claude_response *resp,
    struct claude_tool_result *results,   /* Plan 10 の構造体 */
    int *result_count
);
```

### 初期ツールセット

| ツール名 | 説明 | input_schema | 実装元 |
|---------|------|-------------|--------|
| `read_file` | ファイル内容を読み取る | `{path: string}` | ext3fs |
| `write_file` | ファイルにデータを書き込む | `{path: string, content: string}` | ext3fs |
| `list_dir` | ディレクトリの内容を一覧する | `{path: string}` | ext3fs |
| `run_command` | ユーザランドコマンドを実行して出力を取得する | `{command: string}` | execve + パイプ |
| `get_system_info` | カーネル状態（メモリ、プロセス、デバイス）を取得する | `{}` | カーネル syscall |

### read_file ツールの例

```c
/* src/usr/lib/libagent/tools/tool_read_file.c */

#define READ_FILE_SCHEMA \
    "{\"type\":\"object\"," \
    "\"properties\":{" \
    "  \"path\":{\"type\":\"string\",\"description\":\"File path. Relative paths resolve from the current directory\"}" \
    "}," \
    "\"required\":[\"path\"]}"

PRIVATE int tool_read_file(
    const char *input_json, int input_json_len,
    struct tool_result *result)
{
    struct json_token tokens[16];
    int ntok = json_parse(input_json, input_json_len, tokens, 16);
    if (ntok < 0) {
        result->is_error = 1;
        result->content_len = snprintf(result->content, TOOL_MAX_RESULT,
            "Invalid JSON input");
        return -1;
    }

    const char *path = json_find_string(tokens, ntok, "path");
    if (!path) {
        result->is_error = 1;
        result->content_len = snprintf(result->content, TOOL_MAX_RESULT,
            "Missing required field: path");
        return -1;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        result->is_error = 1;
        result->content_len = snprintf(result->content, TOOL_MAX_RESULT,
            "Cannot open file: %s", path);
        return -1;
    }

    result->content_len = read(fd, result->content, TOOL_MAX_RESULT - 1);
    close(fd);

    if (result->content_len < 0) {
        result->is_error = 1;
        result->content_len = snprintf(result->content, TOOL_MAX_RESULT,
            "Read error on: %s", path);
        return -1;
    }

    result->content[result->content_len] = '\0';
    result->is_error = 0;
    return 0;
}
```

### ツール定義の Claude API 送信

`claude_build_request()` を拡張し、`tools` パラメータを含むリクエストを構築:

```json
{
  "model": "claude-sonnet-4-20250514",
  "max_tokens": 4096,
  "stream": true,
  "system": "You are Sodex OS agent...",
  "tools": [
    {
      "name": "read_file",
      "description": "Read the contents of a file on the Sodex filesystem",
      "input_schema": {"type":"object","properties":{"path":{"type":"string"}},"required":["path"]}
    }
  ],
  "messages": [...]
}
```

### tool_result の会話返送フォーマット

```json
{
  "role": "user",
  "content": [
    {
      "type": "tool_result",
      "tool_use_id": "toolu_01A09q90qw90lq917835lq9",
      "content": "ファイルの内容...",
      "is_error": false
    }
  ]
}
```

複数の tool_use に対して、1 つの user メッセージに複数の tool_result を含める。

## 実装ステップ

1. `tool_registry.h` にレジストリ API を定義する
2. `tool_registry.c` にレジストリ実装（固定配列ベース）を作成する
3. `tool_dispatch.h` / `tool_dispatch.c` にディスパッチャを実装する
4. `tool_read_file.c` — ext3fs からファイル読み取り
5. `tool_write_file.c` — ext3fs へのファイル書き込み
6. `tool_list_dir.c` — ext3fs ディレクトリ一覧
7. `tool_get_system_info.c` — メモリ/プロセス情報取得（syscall 経由）
8. `tool_run_command.c` — コマンド実行（execve + パイプキャプチャ）
9. `claude_build_request_with_tools()` を拡張してツール定義を送信する
10. 各ツールの JSON Schema 文字列を定義する
11. host 単体テストを書く

## テスト

### host 単体テスト (`tests/test_tool_dispatch.c`)

- レジストリにツールを登録 → `tool_registry_find()` で取得できる
- `tool_dispatch()` に read_file の tool_use を渡す → ファイル内容が返る（stub）
- 未登録ツール名 → `is_error: true` の結果が返る
- 入力 JSON が不正 → `is_error: true`

### QEMU スモーク

- モック Claude サーバが tool_use レスポンスを返す
- Sodex 側でツール実行 → tool_result を構築
- 構築された JSON が正しいフォーマットであることをシリアルログで確認
- （API 返送は Plan 13 で結合）

### fixture (`tests/fixtures/tools/`)

- `tool_use_read_file.json` — read_file の tool_use ブロック
- `tool_use_invalid.json` — 不正な tool_use
- `tool_result_success.json` — 期待される tool_result JSON
- `tool_result_error.json` — エラー時の tool_result JSON

## 変更対象

- 新規:
  - `src/usr/include/agent/tool_registry.h`
  - `src/usr/include/agent/tool_dispatch.h`
  - `src/usr/lib/libagent/tool_registry.c`
  - `src/usr/lib/libagent/tool_dispatch.c`
  - `src/usr/lib/libagent/tools/tool_read_file.c`
  - `src/usr/lib/libagent/tools/tool_write_file.c`
  - `src/usr/lib/libagent/tools/tool_list_dir.c`
  - `src/usr/lib/libagent/tools/tool_get_system_info.c`
  - `src/usr/lib/libagent/tools/tool_run_command.c`
  - `tests/test_tool_dispatch.c`
  - `tests/fixtures/tools/`
- 既存:
  - `src/usr/lib/libagent/makefile` — tools サブディレクトリ追加
  - `src/usr/lib/libagent/claude_adapter.c` — `claude_build_request_with_tools()` 拡張

## 完了条件

- ツールレジストリに 5 つのツールが登録される
- tool_use ブロックから対応ハンドラが呼び出される
- tool_result JSON が Anthropic API 仕様に準拠している
- 未登録ツールやエラー時に安全に `is_error: true` が返る
- host 単体テストが通る

## 依存と後続

- 依存: Plan 10 (Claude adapter), Plan 11 (結合テスト)
- 後続: Plan 13 (マルチターン会話), Plan 14 (エージェントループ)
