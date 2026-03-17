# Plan 17: フックシステムと権限管理

## 概要

Claude Agent SDK のフックシステムと権限モデルに相当する機能を実装し、
エージェントのツール実行を制御・監査する仕組みを構築する。
OS カーネルならではの「ハードウェアレベルの保護」と「監査ログ」を
エージェントの行動制御に活用する。

## 目標

- ツール実行前後のフックポイント（PreToolUse / PostToolUse）を提供する
- 権限ポリシーにより危険なツール操作（ファイル削除、プロセス kill）を制限する
- 全ツール実行の監査ログを永続的に記録する
- 設定ファイルによる権限ポリシーの管理

## 背景

Claude Agent SDK の権限モデル:
- `default`: 不明なツールはコールバックで確認
- `acceptEdits`: ファイル操作を自動承認
- `bypassPermissions`: 全自動承認
- `plan`: ツール実行なし

Sodex では「承認コールバック」の代わりに、設定ファイルベースのポリシーと
カーネルの機能を組み合わせる。自律 OS では人間の承認を前提にできないため、
ポリシーは事前定義のルールで判断する。

## 設計

### フックシステム

```c
/* src/usr/include/agent/hooks.h */

/* フックイベント種別 */
enum hook_event {
    HOOK_PRE_TOOL_USE,       /* ツール実行前 */
    HOOK_POST_TOOL_USE,      /* ツール実行後（成功） */
    HOOK_POST_TOOL_FAILURE,  /* ツール実行後（失敗） */
    HOOK_AGENT_START,        /* エージェントループ開始 */
    HOOK_AGENT_STOP,         /* エージェントループ終了 */
    HOOK_STEP_START,         /* ステップ開始 */
    HOOK_STEP_END,           /* ステップ終了 */
};

/* フックコンテキスト */
struct hook_context {
    enum hook_event event;
    const char *tool_name;         /* ツール系イベントのみ */
    const char *tool_input_json;   /* PreToolUse のみ */
    const struct tool_result *tool_result;  /* PostToolUse のみ */
    int step_number;
    int elapsed_ms;
};

/* フックの戻り値 */
enum hook_decision {
    HOOK_CONTINUE,           /* 実行を継続 */
    HOOK_BLOCK,              /* 実行をブロック（PreToolUse のみ有効） */
    HOOK_MODIFY,             /* 入力を変更して継続（PreToolUse のみ） */
};

/* フックレスポンス */
struct hook_response {
    enum hook_decision decision;
    char message[256];             /* ブロック理由など */
    char modified_input[4096];     /* HOOK_MODIFY 時の変更後入力 */
    int  modified_input_len;
};

/* フックハンドラの関数型 */
typedef int (*hook_handler_fn)(
    const struct hook_context *ctx,
    struct hook_response *response
);

/* フック登録 */
int hooks_register(enum hook_event event, hook_handler_fn handler);

/* フック実行（全登録ハンドラを順次呼び出し） */
int hooks_fire(
    const struct hook_context *ctx,
    struct hook_response *response
);
```

### 権限ポリシー

```c
/* src/usr/include/agent/permissions.h */

/* 権限モード */
enum permission_mode {
    PERM_STRICT,             /* 許可リストのみ実行可能 */
    PERM_STANDARD,           /* 読み取りは許可、書き込み/実行は制限 */
    PERM_PERMISSIVE,         /* 全ツール許可（ログのみ） */
};

/* ツール別の権限ルール */
struct tool_permission {
    char tool_name[64];
    int  allowed;            /* 1: 許可, 0: 拒否 */
    int  requires_log;       /* 1: 監査ログ必須 */
    char path_restrict[256]; /* パス制限（read_file/write_file 用）*/
};

/* 権限ポリシー */
struct permission_policy {
    enum permission_mode mode;
    struct tool_permission rules[32];
    int rule_count;

    /* 拒否リスト（モードに関係なく常にブロック） */
    const char *deny_patterns[16];   /* パスのパターン */
    int deny_count;
};

/* ポリシー API */
int  perm_load_policy(struct permission_policy *policy, const char *path);
int  perm_check_tool(const struct permission_policy *policy,
                     const char *tool_name,
                     const char *input_json);
void perm_set_mode(struct permission_policy *policy, enum permission_mode mode);
```

### 権限設定ファイル

```
# /etc/agent/permissions.conf

mode=standard

# ツール別ルール
[read_file]
allowed=yes
log=yes

[write_file]
allowed=yes
log=yes
path_deny=/etc/agent/*      # エージェント自身の設定は書き換え不可
path_deny=/boot/*            # ブート領域は保護

[list_dir]
allowed=yes
log=no

[run_command]
allowed=yes
log=yes
# 危険なコマンドパターン
deny_pattern=rm -rf
deny_pattern=dd if=
deny_pattern=mkfs

[manage_process]
allowed=restricted          # kill は pid 1 以外のみ
log=yes

[get_system_info]
allowed=yes
log=no
```

### 監査ログ

```c
/* src/usr/include/agent/audit.h */

#define AUDIT_LOG_PATH  "/var/agent/audit.log"
#define AUDIT_MAX_ENTRY 512

/* 監査ログエントリ */
struct audit_entry {
    int  timestamp;          /* kernel tick */
    char session_id[33];
    int  step;
    char tool_name[64];
    char action[16];         /* "execute", "blocked", "error" */
    char detail[256];        /* 入力要約またはブロック理由 */
};

/* 監査ログ API */
int audit_init(void);
int audit_log(const struct audit_entry *entry);
int audit_read(struct audit_entry *entries, int max_entries, int *count);
int audit_rotate(int max_size);   /* ログローテーション */
```

監査ログ出力例:
```
[1234567] session=a1b2c3 step=1 EXECUTE read_file path=/etc/hostname
[1234570] session=a1b2c3 step=1 EXECUTE list_dir path=/etc
[1234580] session=a1b2c3 step=2 BLOCKED run_command reason="deny_pattern: rm -rf"
[1234590] session=a1b2c3 step=3 EXECUTE write_file path=/home/report.txt
[1234600] session=a1b2c3 step=3 ERROR run_command reason="timeout after 10000ms"
```

### agent.c への統合

```c
/* agent.c — ツール実行部分の変更 */

PRIVATE int agent_execute_tool(
    struct agent_state *state,
    const struct claude_tool_use *tool_use,
    struct tool_result *result)
{
    struct hook_context ctx = {
        .event = HOOK_PRE_TOOL_USE,
        .tool_name = tool_use->name,
        .tool_input_json = tool_use->input_json,
        .step_number = state->current_step,
    };
    struct hook_response hook_resp;

    /* PreToolUse フック */
    hooks_fire(&ctx, &hook_resp);

    if (hook_resp.decision == HOOK_BLOCK) {
        result->is_error = 1;
        snprintf(result->content, TOOL_MAX_RESULT,
            "Tool blocked by policy: %s", hook_resp.message);
        audit_log_blocked(state, tool_use, hook_resp.message);
        return 0;  /* エラーだがエージェントは継続 */
    }

    /* 権限チェック */
    if (!perm_check_tool(&policy, tool_use->name, tool_use->input_json)) {
        result->is_error = 1;
        snprintf(result->content, TOOL_MAX_RESULT,
            "Permission denied for tool: %s", tool_use->name);
        audit_log_denied(state, tool_use);
        return 0;
    }

    /* ツール実行 */
    const char *input = (hook_resp.decision == HOOK_MODIFY)
        ? hook_resp.modified_input
        : tool_use->input_json;

    int err = tool_dispatch_by_name(tool_use->name, input, strlen(input), result);

    /* PostToolUse フック */
    ctx.event = err ? HOOK_POST_TOOL_FAILURE : HOOK_POST_TOOL_USE;
    ctx.tool_result = result;
    hooks_fire(&ctx, &hook_resp);

    /* 監査ログ */
    audit_log_execute(state, tool_use, result);

    return err;
}
```

### ビルトインフック: パス保護

```c
/* デフォルトで登録されるフック: 保護パスへの書き込みをブロック */
PRIVATE int hook_protect_paths(
    const struct hook_context *ctx,
    struct hook_response *response)
{
    if (ctx->event != HOOK_PRE_TOOL_USE)
        return 0;
    if (strcmp(ctx->tool_name, "write_file") != 0 &&
        strcmp(ctx->tool_name, "run_command") != 0)
        return 0;

    /* /boot/*, /etc/agent/* への書き込みをブロック */
    const char *protected[] = {"/boot/", "/etc/agent/", NULL};
    /* ... パスチェック ... */

    return 0;
}
```

## 実装ステップ

1. `hooks.h` にフックシステム API を定義する
2. `hooks.c` にフック登録・実行エンジンを実装する
3. `permissions.h` に権限ポリシー構造体と API を定義する
4. `permissions.c` に権限チェックロジックを実装する
5. `perm_load_policy()` — 設定ファイルのパーサを実装する
6. `audit.h` / `audit.c` — 監査ログの書き込み・読み取り・ローテーション
7. `/etc/agent/permissions.conf` のデフォルト設定を作成する
8. `agent.c` の `agent_execute_tool()` にフック・権限チェックを統合する
9. ビルトインフック（パス保護、危険コマンド拒否）を登録する
10. `agent audit` サブコマンドで監査ログを閲覧できるようにする
11. host 単体テストを書く

## テスト

### host 単体テスト (`tests/test_hooks_permissions.c`)

- フック登録 → PreToolUse で呼ばれる
- HOOK_BLOCK → ツール実行がスキップされ is_error 結果が返る
- HOOK_MODIFY → 変更された入力でツール実行
- 権限設定の読み込み → 正しいルールが適用される
- path_deny パターン → 該当パスへの write_file がブロック
- deny_pattern → 危険コマンドがブロック
- 監査ログへの書き込みと読み込み

### QEMU スモーク

- `agent run "Write a file to /boot/test.txt"` → ブロックされて Claude にエラー返却
- `agent run "Delete all files in /tmp"` → deny_pattern でブロック
- `agent audit` → 上記のブロックイベントが表示される
- `agent run "Read /etc/hostname"` → 通常通り成功（ログ記録）

## 変更対象

- 新規:
  - `src/usr/include/agent/hooks.h`
  - `src/usr/include/agent/permissions.h`
  - `src/usr/include/agent/audit.h`
  - `src/usr/lib/libagent/hooks.c`
  - `src/usr/lib/libagent/permissions.c`
  - `src/usr/lib/libagent/audit.c`
  - `src/rootfs-overlay/etc/agent/permissions.conf`
  - `tests/test_hooks_permissions.c`
- 既存:
  - `src/usr/lib/libagent/agent.c` — フック・権限統合
  - `src/usr/command/agent.c` — `agent audit` サブコマンド追加

## 完了条件

- PreToolUse フックでツール実行をブロックできる
- 権限設定ファイルでツール別の許可/拒否が制御できる
- 保護パスへの書き込みがブロックされる
- 危険なコマンドパターンがブロックされる
- 全ツール実行が監査ログに記録される
- host 単体テストが通る

## 依存と後続

- 依存: Plan 14 (エージェントループ), Plan 15 (ツール設計)
- 後続: Plan 18 (結合テスト)
