# Plan 20: 対話モードと継続メモリー

## 概要

現在の `agent` は 1 回の prompt を送って終了する単発 CLI であり、
Claude Code や Codex のような「起動中は会話を続けられ、終了後も前回文脈を再開できる」
体験になっていない。

本 Plan では `agent` を既定で対話 REPL に拡張し、
会話履歴・長期メモリー・コンテキスト圧縮を分離して管理する。
これにより、同一セッション中の継続会話だけでなく、
前回のやりとりを踏まえた再開も実現する。

## 目標

- `agent` を既定で対話 REPL とする
- `agent "質問"` を「初回メッセージ付き REPL」とする
- `agent -p "質問"` と `agent run "タスク"` で単発モードを維持する
- `agent --continue` / `agent --resume [id]` で前回セッションを再開できる
- 会話履歴を full-fidelity で保存し、`tool_use` / `tool_result` も復元できる
- user-scope `/etc/CLAUDE.md` と project-scope `${cwd}/CLAUDE.md` を起動時に自動ロードする
- `src/rootfs-overlay/etc/CLAUDE.md` に Sodex シェルの現状能力を書いた seed を同梱する
- 長時間会話では compaction/checkpoint を行い、重要状態を保ったまま継続する
- `/clear`, `/compact`, `/memory`, `/permissions`, `!cmd`, `# メモ` を提供する

## Web 調査メモ（2026-03-18 確認）

### Claude Code から採るべき要件

- `claude` は interactive REPL、`claude "query"` は初回 prompt 付き REPL、`claude -p` は単発実行
- `--continue` は現在ディレクトリの直近会話を再開し、`--resume` は特定セッション再開
- 再開時は「全メッセージ履歴」「tool state」「元の model/config」が復元される
- memory は複数階層で管理され、起動時に自動ロードされる
- `#` で memory 追記、`/memory` で memory 編集、`/clear` と `/compact` がある
- interactive mode では `!` で Bash 実行、その出力がセッションに追加される
- command history は working directory 単位で保持される

### Codex / OpenAI から採るべき要件

- agent loop は「ユーザー入力 → 推論 → tool call → tool output を次ターンへ付加 → 完了」 を繰り返す
- 会話が長くなると prompt も伸びるため、context management は agent 側の責務になる
- Codex は compaction を使って長時間タスクでも高価値な状態を保持しながら継続する
- shell 出力は大きすぎると文脈を汚すため、bounded output と artifact 化が有効

### Sodex への適用方針

- Claude Code の REPL / resume / memory 体験を UI 仕様の基準にする
- Codex の compaction / bounded output / working-directory 指向を会話管理仕様に取り込む
- OpenAI Responses API の native compaction は Anthropic Messages API では使えないため、
  Sodex では client-side checkpoint compaction を実装する

## 背景

既存 Plan の責務は分散している:

- Plan 13: マルチターン会話
- Plan 16: セッション永続化
- Plan 19: `agent` CLI 実用化

しかし、現状は以下が欠けている:

- `agent` 起動後に会話を続ける REPL
- `tool_use` / `tool_result` を含む完全なセッション再開
- 複数階層 memory の自動ロード
- context 上限に達した際の compaction
- slash command / shell passthrough を含む実運用 UI

本 Plan は Plan 13 / 16 / 19 を横断する上位 UX 仕様である。

## CLI / UX 設計

### コマンド体系

```sh
agent
agent "この repo の構成を説明して"
agent -p "この 1 問だけ答えて"
agent run "テストを調べて原因を報告して"
agent --continue
agent --resume
agent --resume <session-id>
agent sessions
agent sessions --delete <session-id>
agent memory
```

### 動作ルール

- `agent`
  - 新規 interactive REPL を開始する
  - 開始前に `/etc/CLAUDE.md` と `${cwd}/CLAUDE.md` を読んで context に注入する
- `agent "質問"`
  - REPL を開始し、最初の 1 ターンを自動送信する
  - 最初の API 呼び出し前に `/etc/CLAUDE.md` と `${cwd}/CLAUDE.md` を注入する
- `agent -p "質問"`
  - 非対話。最終応答を出して終了する
- `agent run "タスク"`
  - 既存の自律実行モード。対話 REPL ではなく 1 ジョブとして完了まで回す
- `agent --continue`
  - 現在の `cwd` に紐づく直近セッションを無条件で再開する
- `agent --resume`
  - TTY 制約を考慮し、フルスクリーン picker ではなく番号選択式一覧を出す
- `agent --resume <id>`
  - 指定セッションを再開する
  - 再開前に `/etc/CLAUDE.md` と「現在の `cwd` 直下の `CLAUDE.md`」を再読込し、前回 session state に上書きマージする

### 互換方針

- 現行の単発用途は `agent -p` と `agent run` に移す
- `agent <prompt>` の意味は Claude Code に合わせて「初回 prompt 付き REPL」に変更する
- 必要なら移行期間だけ `agent --oneshot <prompt>` を残してもよいが、既定は REPL を優先する

### REPL プロンプト

```text
[agent main@/src max=10 ctx=42% perm=standard] >
```

表示項目:

- session 名または短い session id
- 現在の `cwd`
- `max_steps`
- context 使用率
- permission mode

終了方法:

- `exit`
- `quit`
- `Ctrl+D`

## Slash Command とショートカット

### 必須 slash command

| コマンド | 役割 |
|---|---|
| `/help` | 対話ヘルプ |
| `/clear` | 現在会話を閉じて新規セッション開始 |
| `/compact [focus]` | 手動 compaction |
| `/memory` | 読み込まれた memory 一覧と編集 |
| `/permissions [mode]` | permission mode の確認/変更 |
| `/resume [id]` | セッション再開 |
| `/sessions` | セッション一覧 |
| `/rename <name>` | session 名変更 |
| `/status` | model / tokens / memory / session 情報 |

### ショートカット

- `# メモ内容`
  - workspace memory へのクイック追記
- `!command`
  - `run_command` 相当でシェル実行し、出力を会話に追加する

## セッションモデル

### ファイル配置

```text
/var/agent/
  sessions/
    index.jsonl
    <session-id>.jsonl
  memory/
    global.md
    <cwd-hash>.md
```

### セッションメタデータ

```c
/* src/usr/include/agent/session.h */

#define SESSION_ID_LEN      32
#define SESSION_NAME_LEN    64
#define SESSION_CWD_LEN    256
#define SESSION_SUMMARY_LEN 160

struct session_meta {
    char id[SESSION_ID_LEN + 1];
    char name[SESSION_NAME_LEN];
    char cwd[SESSION_CWD_LEN];
    unsigned int cwd_hash;
    int created_at;
    int last_active_at;
    int turn_count;
    int total_tokens;
    int compact_count;
    char model[64];
    char summary[SESSION_SUMMARY_LEN];
};
```

### JSONL 形式

各行は append-only event とする。

```jsonl
{"type":"meta","id":"...","cwd":"/src","cwd_hash":1234,"model":"claude-sonnet-4-20250514","name":"main"}
{"type":"message","role":"user","content":[{"type":"text","text":"src/usr を見て"}]}
{"type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_01","name":"list_dir","input":{"path":"/src/usr"}}],"stop_reason":"tool_use","input_tokens":120,"output_tokens":24}
{"type":"message","role":"user","content":[{"type":"tool_result","tool_use_id":"toolu_01","content":"...","is_error":false}]}
{"type":"compact","summary":"src/usr には ... 未解決は ...","from_turn":0,"to_turn":7}
{"type":"rename","name":"usr-inspection"}
```

### 復元要件

- `session_load()` は text だけでなく `tool_use` / `tool_result` / `compact` を復元する
- 直近 compact があれば、その summary を synthetic system note として再注入する
- compact 以降の raw turn は完全復元する
- session 再開時に `cwd`, `model`, `permission mode`, memory source を元に戻す

## Memory 階層

### ロード順序

1. system policy
   - `/etc/agent/system_prompt.txt`
   - `/etc/agent/permissions.conf`
2. user-scope instruction
   - `/etc/CLAUDE.md`
   - rootfs overlay に seed ファイルを同梱する
   - 存在すれば毎回読み込む
   - role は `user_scope_instruction`
3. project-scope instruction
   - `${cwd}/CLAUDE.md`
   - 存在すれば必須で読み込む
   - role は `project_scope_instruction`
4. global user memory
   - `/var/agent/memory/global.md`
5. project memory
   - `cwd` から `/` 手前まで遡って `AGENTS.md`, `AGENTS.local.md`, `CLAUDE.md`, `CLAUDE.local.md` を探索
   - ただし `${cwd}/CLAUDE.md` は 3 ですでに読んでいるため再注入しない
   - `/etc/CLAUDE.md` は user-scope なので探索対象に含めない
6. workspace auto memory
   - `/var/agent/memory/<cwd-hash>.md`
7. session checkpoint
   - 最新 `compact` summary + 直近 raw turn

### user-scope `/etc/CLAUDE.md`

- 読み込み対象は固定で `/etc/CLAUDE.md`
- agent の初回起動、`-p`、`run`、`--continue`、`--resume` の全モードで毎回読む
- このファイルには「Sodex 全体で共通の能力と運用指示」を書く
- 今回の初期内容として、shell 構文、主要コマンド、UTF-8/IME/`vi` の可否を記述する
- 起動ログに
  - `[AGENT] loaded user CLAUDE.md: /etc/CLAUDE.md (<bytes> bytes)`
  を出す
- 長期 auto memory とは別レイヤで扱い、自動更新しない

### project-scope `${cwd}/CLAUDE.md`

- 読み込み対象は「agent を起動したカレントディレクトリ直下」のみとする
- パスは `${cwd}/CLAUDE.md` 固定で、親ディレクトリ探索より先に読む
- 存在しない場合は無視して続行する
- 存在する場合は起動ログに
  - `[AGENT] loaded project CLAUDE.md: <path> (<bytes> bytes)`
  を出す
- セッション再開時も毎回再読込する
- 同一 session を別ディレクトリから再開した場合は「現在の `cwd` の `CLAUDE.md`」を優先する
- 用途は「その作業ディレクトリ固有の指示注入」であり、長期 memory として自動保存はしない

### project memory の扱い

- `AGENTS.md` を優先し、なければ `CLAUDE.md` も読む
- 近いディレクトリの memory を後勝ちで適用する
- ただし `/etc/CLAUDE.md` は user-scope、`${cwd}/CLAUDE.md` は project-scope として別レイヤで扱う
- `read_file` / `list_dir` / `write_file` / `run_command` が特定 subtree を触った場合、
  その subtree 内の `AGENTS.md` / `CLAUDE.md` を lazy load して次ターンから反映する

### auto memory に保存する情報

保存してよい:

- よく使う build/test コマンド
- repo 特有の規約
- よく参照するパス
- 継続作業で毎回必要になる注意点
- ユーザーが明示した好み

保存してはいけない:

- API key / token / password
- 大きな生ログ
- 一時的な調査結果だけで再利用価値がないもの
- 既に session transcript に残っているだけの短命情報

### memory 書き込みモード

```c
enum agent_memory_mode {
    AGENT_MEMORY_OFF = 0,
    AGENT_MEMORY_MANUAL,
    AGENT_MEMORY_AUTO,
};
```

既定は `AGENT_MEMORY_AUTO` としつつ、
秘密情報らしき文字列を検出した場合は自動保存せず監査ログだけ残す。

## Compaction / Context 管理

### 方針

- transcript は永続ファイルに全量保存する
- prompt に毎回入れるのは
  - system prompt
  - memory 群
  - 最新 compact summary
  - 直近 raw turn
  - 直近 tool result の要約
  のみとする

### trigger

- 手動: `/compact [focus]`
- 自動:
  - `context_usage >= compact_threshold_pct`
  - `turn_count >= compact_turn_threshold`
  - `tool_result_bytes >= compact_tool_output_threshold`

### compact の生成方法

1. 最後の checkpoint 以降の会話を入力にする
2. 専用 prompt で「継続に必要な状態だけ」を要約する
3. summary に必ず残す
   - ユーザーの目的
   - すでに確定した事実
   - 重要なファイルパス / コマンド / エラー
   - 未完了タスク
   - 次ターンで再開すべき論点
4. summary を `compact` event として保存する
5. active conversation では summary + 直近 N ターンだけ残す

### fallback

LLM compaction が失敗した場合は deterministic fallback を使う:

- user prompt の先頭一覧
- assistant 最終文
- 実行した tool 名と主要 path
- 失敗した command / error code

### bounded output

`run_command` と `read_file` の大きな出力は会話へ丸ごと入れない:

- 会話には head/tail 抜粋と省略バイト数だけ入れる
- 全文は artifact file に退避し、path を一緒に返す

## API / データ構造

```c
/* src/usr/include/agent/agent.h */

struct agent_config {
    const char *model;
    char system_prompt[AGENT_MAX_SYSTEM_PROMPT];
    int  system_prompt_len;
    int  max_steps;
    int  max_tokens_per_turn;
    const char *terminal_tool;
    const char *api_key;
    const struct llm_provider *provider;

    /* 対話モード設定 */
    int interactive_default;
    enum agent_memory_mode memory_mode;
    int compact_threshold_pct;
    int keep_recent_turns;
    int keep_recent_tool_results;
};

struct agent_session_state {
    struct session_meta meta;
    int resumed;
    int dirty_memory;
};

int agent_repl_run(
    const struct agent_config *config,
    const char *initial_prompt,
    const char *resume_session_id
);
```

補助 API:

```c
int agent_resume_latest_for_cwd(
    const struct agent_config *config,
    const char *cwd,
    char *session_id_out
);

int agent_load_memory_context(
    const char *cwd,
    struct conversation *conv
);

int agent_load_user_claude_md(
    struct conversation *conv
);

int agent_load_project_claude_md(
    const char *cwd,
    struct conversation *conv
);

int conv_compact(
    const struct agent_config *config,
    struct conversation *conv,
    struct session_meta *meta,
    const char *focus
);
```

## 実装ステップ

1. `agent` の CLI 仕様を REPL 既定に更新する
2. `agent_repl_run()` を追加し、入力待ちと streaming 出力を実装する
3. slash command parser を実装する
4. `!cmd` と `# memory` のショートカットを実装する
5. `session_meta` を拡張し、`cwd_hash` / `name` / `last_active_at` を保存する
6. `session_append_turn()` を full-fidelity JSONL に更新する
7. `session_load()` を full-fidelity restore に更新する
8. `--continue` / `--resume` / `sessions` / `rename` を CLI に統合する
9. user-scope `/etc/CLAUDE.md` seed を rootfs overlay に追加する
10. user-scope `/etc/CLAUDE.md` と project-scope `${cwd}/CLAUDE.md` loader を実装し、初回起動と resume 前に必ず注入する
11. memory loader を実装する（system/global/project/workspace/lazy subtree）
12. auto memory 抽出と secret filter を実装する
13. `conv_compact()` を実装する
14. bounded output を `run_command` / `read_file` に統合する
15. host unit test を追加する
16. QEMU で multi-turn → exit → continue → compact の一連動作を確認する

## テスト

### host 単体テスト

- `test_agent_repl_cli.c`
  - `agent`, `agent -p`, `agent run`, `agent --continue`, `agent --resume`
- `test_session_restore_full.c`
  - `tool_use` / `tool_result` / `compact` を含む JSONL を完全復元
- `test_memory_loader.c`
  - `/etc/CLAUDE.md` と `${cwd}/CLAUDE.md` が起動時に読み込まれる
  - `/etc/CLAUDE.md` → `${cwd}/CLAUDE.md` → 補助 project memory → workspace memory の優先順位
- `test_compaction.c`
  - compact 生成後に summary + recent turn だけが active context に残る
- `test_bounded_output.c`
  - 長い command 出力が head/tail + artifact path に変換される

### QEMU smoke

1. `agent "まず /etc を見て"` で REPL 開始
2. 2 ターン目で「前回の一覧から agent.conf を読んで」と入力
3. `exit` 後に `agent --continue` で再開
4. 「さっき何を見た？」に答えられることを確認
5. 長い出力を伴う command を実行して auto compaction を発火
6. compaction 後も未完了タスクを継続できることを確認

## 変更対象

- 新規:
  - `src/rootfs-overlay/etc/CLAUDE.md`
  - `src/usr/lib/libagent/repl.c`
  - `src/usr/lib/libagent/memory_store.c`
  - `src/usr/lib/libagent/compaction.c`
  - `tests/test_agent_repl_cli.c`
  - `tests/test_session_restore_full.c`
  - `tests/test_memory_loader.c`
  - `tests/test_compaction.c`
  - `tests/test_bounded_output.c`
- 既存:
  - `src/usr/command/agent.c`
  - `src/usr/lib/libagent/agent_loop.c`
  - `src/usr/lib/libagent/conversation.c`
  - `src/usr/lib/libagent/session.c`
  - `src/usr/lib/libagent/tool_run_command.c`
  - `src/usr/include/agent/agent.h`
  - `src/usr/include/agent/conversation.h`
  - `src/usr/include/agent/session.h`
  - `src/rootfs-overlay/etc/agent/agent.conf`

## 完了条件

- `agent` 起動中に複数ターンの対話が継続できる
- `agent --continue` / `agent --resume` で前回文脈を再開できる
- `tool_use` / `tool_result` を含む会話履歴が復元される
- `/etc/CLAUDE.md` と `${cwd}/CLAUDE.md` が agent 起動時に読み込まれ、最初の応答から反映される
- `AGENTS.md` / 補助 `CLAUDE.md` / workspace memory が自動ロードされる
- context 上限時に compaction が動作し、会話品質が極端に落ちない
- host unit test と QEMU smoke が通る

## 参考

- Anthropic, Claude Code CLI reference  
  https://docs.anthropic.com/en/docs/claude-code/cli-reference
- Anthropic, Common workflows  
  https://docs.anthropic.com/en/docs/claude-code/tutorials
- Anthropic, Manage Claude's memory  
  https://docs.anthropic.com/en/docs/claude-code/memory
- Anthropic, Interactive mode  
  https://docs.anthropic.com/en/docs/claude-code/interactive-mode
- Anthropic, Slash commands  
  https://docs.anthropic.com/en/docs/claude-code/slash-commands
- OpenAI, Unrolling the Codex agent loop  
  https://openai.com/index/unrolling-the-codex-agent-loop/
- OpenAI, From model to agent: Equipping the Responses API with a computer environment  
  https://openai.com/index/equip-responses-api-computer-environment/
