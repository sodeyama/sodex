# Plan 16: セッション永続化

## 概要

エージェントの会話履歴を ext3 ファイルシステム上に永続化し、
再起動やクラッシュ後にセッションを再開できるようにする。
Claude Agent SDK が `~/.claude/projects/` に JSONL でセッションを保存するパターンに
準じた設計をとる。

## 目標

- 会話履歴を JSONL 形式で ext3fs に保存する
- セッション ID で過去の会話を特定し再開できる
- セッション一覧の表示と削除ができる
- 保存容量の上限管理（古いセッションの自動削除）

## 背景

Claude Agent SDK のセッション管理:
- パス: `~/.claude/projects/<encoded-cwd>/<session-id>.jsonl`
- 各行が 1 メッセージの JSON
- `resume` オプションでセッション再開
- `fork_session` でセッション分岐

Sodex ではパスを `/var/agent/sessions/` に変更し、
同等の JSONL 形式でセッションを管理する。

## 設計

### ファイルレイアウト

```
/var/agent/
  └── sessions/
      ├── index.dat        — セッション一覧（バイナリインデックス）
      ├── <session-id>.jsonl  — セッションファイル
      └── ...
```

### セッション ID

```c
/* 32文字の16進数 ID（128bit）*/
#define SESSION_ID_LEN  32

/* PRNG から生成 */
void session_generate_id(char *id_buf);
```

### セッションファイルフォーマット (JSONL)

各行が 1 メッセージ。先頭行はメタデータ:

```jsonl
{"type":"meta","session_id":"a1b2c3...","created_at":12345678,"model":"claude-sonnet-4-20250514","system_prompt_hash":"abcdef12"}
{"type":"user","content":"List the files in /etc"}
{"type":"assistant","content":[{"type":"text","text":"I'll check the directory."},{"type":"tool_use","id":"toolu_01","name":"list_dir","input":{"path":"/etc"}}],"stop_reason":"tool_use","input_tokens":50,"output_tokens":30}
{"type":"user","content":[{"type":"tool_result","tool_use_id":"toolu_01","content":"hostname\nagent\nclaude.conf","is_error":false}]}
{"type":"assistant","content":[{"type":"text","text":"The /etc directory contains: hostname, agent, claude.conf"}],"stop_reason":"end_turn","input_tokens":120,"output_tokens":25}
```

### セッション管理 API

```c
/* src/usr/include/agent/session.h */

#define SESSION_MAX_SESSIONS    32
#define SESSION_DIR            "/var/agent/sessions"

/* セッションメタデータ */
struct session_meta {
    char id[SESSION_ID_LEN + 1];
    int  created_at;         /* kernel tick */
    int  turn_count;
    int  total_tokens;
    char model[64];
};

/* セッションインデックス */
struct session_index {
    struct session_meta entries[SESSION_MAX_SESSIONS];
    int count;
};

/* セッションの作成 */
int session_create(struct session_meta *meta, const char *model);

/* 会話ターンの追記（JSONL の 1 行追加） */
int session_append_turn(
    const char *session_id,
    const struct conv_turn *turn,
    int input_tokens,
    int output_tokens
);

/* セッションの読み込み（conversation 構造体に復元） */
int session_load(
    const char *session_id,
    struct conversation *conv
);

/* セッション一覧の取得 */
int session_list(struct session_index *index);

/* セッションの削除 */
int session_delete(const char *session_id);

/* 古いセッションの自動クリーンアップ */
int session_cleanup(int max_sessions);
```

### conversation 構造体との統合

```c
/* conversation.h に追加 */

struct conversation {
    /* ... 既存フィールド ... */

    /* セッション関連 */
    char session_id[SESSION_ID_LEN + 1];
    int  is_persistent;      /* 1: ファイルに永続化する */
};

/* セッション付き会話の開始 */
int conv_start_session(
    struct conversation *conv,
    const char *system_prompt,
    const char *model
);

/* 既存セッションの再開 */
int conv_resume_session(
    struct conversation *conv,
    const char *session_id
);
```

### JSONL ライター/リーダー

```c
/* セッションファイルへの 1 行書き込み */
PRIVATE int session_write_line(int fd, const char *json_line, int len)
{
    int written = write(fd, json_line, len);
    if (written != len) return -1;
    written = write(fd, "\n", 1);
    if (written != 1) return -1;
    return 0;
}

/* セッションファイルの全行読み込み（行コールバック） */
typedef int (*session_line_cb)(const char *line, int len, void *ctx);

int session_read_lines(const char *session_id, session_line_cb cb, void *ctx);
```

### chat / agent コマンドとの統合

```c
/* chat コマンド */
void cmd_chat(int argc, char **argv)
{
    /* chat                    — 新規セッション */
    /* chat --resume <id>      — セッション再開 */
    /* chat --list             — セッション一覧 */
    /* chat --delete <id>      — セッション削除 */
}

/* agent コマンド */
void cmd_agent(int argc, char **argv)
{
    /* agent run "task"             — 新規セッション（自動保存） */
    /* agent run --resume <id> ""   — 前回の続きから実行 */
    /* agent sessions               — セッション一覧 */
    /* agent sessions --delete <id> — セッション削除 */
}
```

### 容量管理

```c
#define SESSION_MAX_FILE_SIZE   (64 * 1024)   /* 64KB per session */
#define SESSION_MAX_TOTAL_SIZE  (512 * 1024)  /* 512KB total */

/* セッションファイルがサイズ上限を超えたら古いターンを切り捨て */
int session_truncate_old_turns(const char *session_id, int max_size);

/* 総容量超過時は最古のセッションを削除 */
int session_cleanup(int max_sessions);
```

## 実装ステップ

1. `session.h` にセッション管理 API を定義する
2. `session.c` に `session_create()` — ID 生成とメタデータ行書き込み
3. `session_append_turn()` — JSONL 形式での 1 行追記
4. `session_load()` — JSONL を読み込んで conversation に復元
5. `session_list()` — `/var/agent/sessions/` を走査してメタデータ収集
6. `session_delete()` — ファイル削除
7. `session_cleanup()` — 容量ベースの自動クリーンアップ
8. `conversation.c` に `conv_start_session()` / `conv_resume_session()` 追加
9. `agent.c` のループ内で各ターン後に `session_append_turn()` 呼び出し追加
10. `chat` / `agent` コマンドにセッション関連サブコマンドを追加
11. host 単体テストを書く

## テスト

### host 単体テスト (`tests/test_session.c`)

- セッション作成 → ファイルが生成される
- 3 ターン追記 → JSONL が 4 行（meta + 3 turns）
- セッション読み込み → conversation が正しく復元される
- セッション一覧 → 作成した数だけ返る
- セッション削除 → ファイルが消える
- クリーンアップ → 最古のセッションが削除される

### QEMU スモーク

- `agent run "What is my hostname?"` → セッションファイルが作成される
- `agent sessions` → 上記セッションが一覧に表示される
- `chat --resume <id>` → 前回の会話の続きができる
- `agent sessions --delete <id>` → セッションが削除される

## 変更対象

- 新規:
  - `src/usr/include/agent/session.h`
  - `src/usr/lib/libagent/session.c`
  - `tests/test_session.c`
  - `src/rootfs-overlay/var/agent/sessions/` (ディレクトリ作成)
- 既存:
  - `src/usr/include/agent/conversation.h` — セッションフィールド追加
  - `src/usr/lib/libagent/conversation.c` — セッション統合
  - `src/usr/lib/libagent/agent.c` — ターン後の自動保存
  - `src/usr/command/chat.c` — セッションサブコマンド
  - `src/usr/command/agent.c` — セッションサブコマンド

## 完了条件

- エージェント実行後に `/var/agent/sessions/<id>.jsonl` が作成される
- セッション再開で前回の会話の続きが正しくできる
- セッション一覧・削除が動作する
- 容量上限でクリーンアップが動作する
- host 単体テストが通る

## 依存と後続

- 依存: Plan 13 (マルチターン会話), Plan 14 (エージェントループ)
- 後続: Plan 17 (フックと権限)
