# Plan 15: システムプロンプトとツール設計

## 概要

Sodex エージェントが自律的に OS を操作するためのシステムプロンプトと、
OS 固有ツールの設計・調整を行う。Claude Agent SDK が `.claude/CLAUDE.md` で
プロジェクト文脈を伝えるように、Sodex はシステムプロンプトで
カーネルの能力と制約を Claude に伝える。

## 目標

- Sodex の能力と制約を正確に伝えるシステムプロンプトを設計する
- 各ツールの description と input_schema を Claude が正しく活用できるよう最適化する
- OS 管理タスク（ファイル操作、プロセス管理、診断）を効果的に実行できるツールセットを確定する
- ツール呼び出しの成功率をモニタリングし、プロンプトを反復改善する

## 背景

エージェントループ（Plan 14）が動作した後、その「知性」の質はシステムプロンプトと
ツール定義に大きく依存する。Claude Agent SDK が CLAUDE.md でコードベースの文脈を
伝えるのと同様、Sodex のエージェントは OS の構造を理解した上で行動する必要がある。

## 設計

### システムプロンプト構造

```
/etc/agent/system_prompt.txt — エージェントのシステムプロンプト
```

4 つのセクションで構成:

```
## 1. アイデンティティ

あなたは Sodex OS のシステムエージェントです。
Sodex は i486 アーキテクチャ向けに独自開発された OS カーネルで、
QEMU 上で動作しています。

## 2. 能力

以下のツールを使って OS を操作できます:
- read_file: ファイルの読み取り
- write_file: ファイルの書き込み
- list_dir: ディレクトリの内容一覧
- run_command: コマンドの実行
- get_system_info: システム情報の取得
- read_serial: シリアルポートからの入力読み取り
- manage_process: プロセスの管理（一覧、停止）

## 3. 制約

- このOSは外部ライブラリを使用しない独自実装です
- メモリは限られています。大量のデータを一度に処理しないでください
- ネットワークアクセスはありますが、帯域は限定的です
- ファイルシステムは ext3 です

## 4. 行動指針

- ツールを使う前に、何をしようとしているか説明してください
- エラーが発生したら、原因を分析してリカバリを試みてください
- 不明な点はユーザーに質問してください
- 破壊的な操作（ファイル削除、プロセス強制停止）の前に確認してください
```

### ツール定義の最適化

各ツールの `description` と `input_schema` を、Claude が正確に使えるよう設計:

#### read_file — 改良版

```json
{
  "name": "read_file",
  "description": "Read the contents of a file on the Sodex ext3 filesystem. Returns the file contents as text. Files larger than 8KB will be truncated. Use list_dir first to discover file paths.",
  "input_schema": {
    "type": "object",
    "properties": {
      "path": {
        "type": "string",
        "description": "Absolute file path (e.g., /etc/hostname, /home/config.txt)"
      },
      "offset": {
        "type": "integer",
        "description": "Byte offset to start reading from. Default: 0"
      },
      "limit": {
        "type": "integer",
        "description": "Maximum bytes to read. Default: 8192"
      }
    },
    "required": ["path"]
  }
}
```

#### write_file

```json
{
  "name": "write_file",
  "description": "Write content to a file on the Sodex ext3 filesystem. Creates the file if it doesn't exist, overwrites if it does. Parent directory must exist.",
  "input_schema": {
    "type": "object",
    "properties": {
      "path": {
        "type": "string",
        "description": "Absolute file path to write"
      },
      "content": {
        "type": "string",
        "description": "Content to write to the file"
      }
    },
    "required": ["path", "content"]
  }
}
```

#### list_dir

```json
{
  "name": "list_dir",
  "description": "List the contents of a directory on the Sodex ext3 filesystem. Returns filenames, types (file/dir), and sizes.",
  "input_schema": {
    "type": "object",
    "properties": {
      "path": {
        "type": "string",
        "description": "Absolute directory path (e.g., /, /etc, /home)"
      }
    },
    "required": ["path"]
  }
}
```

#### run_command

```json
{
  "name": "run_command",
  "description": "Execute a command in the Sodex userland shell and capture its output. Available commands include standard utilities. Timeout: 10 seconds. Output is limited to 4KB.",
  "input_schema": {
    "type": "object",
    "properties": {
      "command": {
        "type": "string",
        "description": "Command to execute (e.g., 'ps', 'uptime', 'cat /etc/hostname')"
      },
      "timeout_ms": {
        "type": "integer",
        "description": "Execution timeout in milliseconds. Default: 10000"
      }
    },
    "required": ["command"]
  }
}
```

#### get_system_info

```json
{
  "name": "get_system_info",
  "description": "Get current system information from the Sodex kernel. Returns memory usage, process list, uptime, and device status as JSON.",
  "input_schema": {
    "type": "object",
    "properties": {
      "category": {
        "type": "string",
        "enum": ["all", "memory", "processes", "devices", "network"],
        "description": "Category of system information to retrieve. Default: all"
      }
    }
  }
}
```

#### manage_process

```json
{
  "name": "manage_process",
  "description": "Manage processes on the Sodex OS. Can list, inspect, or signal processes.",
  "input_schema": {
    "type": "object",
    "properties": {
      "action": {
        "type": "string",
        "enum": ["list", "info", "kill", "nice"],
        "description": "Action to perform"
      },
      "pid": {
        "type": "integer",
        "description": "Process ID (required for info, kill, nice)"
      },
      "signal": {
        "type": "integer",
        "description": "Signal number for kill action. Default: 15 (SIGTERM)"
      }
    },
    "required": ["action"]
  }
}
```

### プロンプトのファイルシステム配置

```
/etc/agent/
  ├── system_prompt.txt    — メインシステムプロンプト
  ├── tool_schemas.json    — 全ツールの JSON Schema 定義
  └── agent.conf           — エージェント設定
       model=claude-sonnet-4-20250514
       max_steps=10
       max_tokens=4096
```

### ビルド時のプロンプト埋め込み

```makefile
# src/makefile — system_prompt を rootfs に埋め込む
AGENT_PROMPT = $(ROOTFS_OVERLAY)/etc/agent/system_prompt.txt
$(AGENT_PROMPT): specs/agent-transport/prompts/system_prompt.txt
	mkdir -p $(dir $@)
	cp $< $@
```

### プロンプト読み込み

```c
/* agent.c 内 */
PRIVATE int agent_load_config(struct agent_config *config)
{
    /* /etc/agent/system_prompt.txt からシステムプロンプトを読み込む */
    int fd = open("/etc/agent/system_prompt.txt", O_RDONLY);
    if (fd < 0) {
        /* フォールバック: ハードコードされた最小プロンプト */
        strncpy(config->system_prompt, DEFAULT_SYSTEM_PROMPT,
                AGENT_MAX_SYSTEM_PROMPT);
        return 0;
    }
    config->system_prompt_len = read(fd, config->system_prompt,
                                     AGENT_MAX_SYSTEM_PROMPT - 1);
    close(fd);
    config->system_prompt[config->system_prompt_len] = '\0';
    return 0;
}
```

### ツール呼び出し成功率のモニタリング

```c
/* ツール実行統計 */
struct tool_stats {
    char name[TOOL_MAX_NAME];
    int  call_count;
    int  success_count;
    int  error_count;
    int  total_ms;           /* 累積実行時間 */
};

/* エージェント終了時にシリアルログに出力 */
void agent_print_tool_stats(const struct agent_state *state);
```

出力例:
```
[AGENT] Tool Statistics:
  read_file:  5 calls, 5 ok, 0 err, avg 12ms
  list_dir:   2 calls, 2 ok, 0 err, avg 8ms
  run_command: 1 calls, 0 ok, 1 err, avg 10200ms (timeout)
```

## 実装ステップ

1. `specs/agent-transport/prompts/system_prompt.txt` にシステムプロンプト草案を作成する
2. 全ツールの JSON Schema を最適化された description 付きで定義する
3. `tool_read_file.c` に offset/limit パラメータを追加する
4. `tool_run_command.c` に timeout パラメータを追加する
5. `tool_manage_process.c` を新規実装する（list, info, kill, nice）
6. `tool_get_system_info.c` に category パラメータを追加する
7. `/etc/agent/` のファイル群を rootfs-overlay に配置する
8. `agent_load_config()` でファイルからプロンプトと設定を読み込む処理を追加する
9. ツール統計のモニタリングと出力を実装する
10. 実 Claude API で各ツールの description を評価し、呼び出し精度を確認する

## テスト

### host 単体テスト

- ツール JSON Schema のバリデーション（JSON としてパース可能であること）
- `tool_manage_process()` のアクション分岐テスト（stub 経由）

### QEMU スモーク

- `agent run "List the files in /etc and read the hostname"` を実行
  → list_dir → read_file の連鎖が正しく動作する
- `agent run "What processes are running?"` を実行
  → get_system_info(processes) が呼ばれる
- ツール統計がシリアルログに出力される

### 手動評価（実 API）

- 様々なタスクを agent に依頼し、ツール選択の適切さを評価
- description の曖昧さによる誤ったツール選択を記録し、改善する

## 変更対象

- 新規:
  - `specs/agent-transport/prompts/system_prompt.txt`
  - `specs/agent-transport/prompts/tool_schemas.json`
  - `src/usr/lib/libagent/tools/tool_manage_process.c`
  - `src/rootfs-overlay/etc/agent/system_prompt.txt`
  - `src/rootfs-overlay/etc/agent/agent.conf`
- 既存:
  - `src/usr/lib/libagent/tools/tool_read_file.c` — offset/limit 追加
  - `src/usr/lib/libagent/tools/tool_run_command.c` — timeout 追加
  - `src/usr/lib/libagent/tools/tool_get_system_info.c` — category 追加
  - `src/usr/lib/libagent/agent.c` — config ファイル読み込み、統計出力
  - `src/makefile` — rootfs-overlay への prompt コピー

## 完了条件

- システムプロンプトが `/etc/agent/system_prompt.txt` から読み込まれる
- 全ツールの description が実 API で正しくツール選択を誘導する
- manage_process ツールが動作する
- ツール統計がエージェント終了時に出力される
- 5 種類以上のタスクで適切なツール選択が行われる（手動評価）

## 依存と後続

- 依存: Plan 12 (ツール実行), Plan 14 (エージェントループ)
- 後続: Plan 16 (セッション永続化), Plan 17 (フックと権限)
