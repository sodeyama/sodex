# Agent Filesystem Tools Tasks

`specs/agent-filesystem-tools/README.md` を着手単位に落としたタスクリスト。
既存の `read_file` / `write_file` / `list_dir` を harden し、
guest 内ファイル操作を安全に運用できる状態まで持っていく。

## 優先順

1. path 境界と permission 契約
2. read/write/list の schema と I/O 契約
3. host/QEMU 検証
4. prompt / README / 運用導線

## M0: path 境界の固定

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | AFT-01 | `read_file` / `write_file` / `list_dir` が absolute path 必須であることを仕様と実装に反映する | なし | 相対 path が一貫して拒否される |
| [x] | AFT-02 | path 正規化 helper を導入し、permission 判定と tool 実装で共通利用する | AFT-01 | `//`, `/./`, `..` を含む path の扱いが固定される |
| [x] | AFT-03 | `permissions.conf` を read/write ごとの allow/deny prefix を持つ形式へ拡張する | AFT-02 | `standard` mode の writable subtree が明示される |
| [x] | AFT-04 | `permissions.c` と builtin hook の責務を整理し、二重管理を減らす | AFT-03 | path 保護が 1 つの契約で説明できる |

## M1: tool 契約の固定

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | AFT-05 | `read_file` の schema を拡張し、必要なら `offset` / `limit` を追加する | AFT-01 | 大きいファイルを分割取得できる |
| [x] | AFT-06 | `write_file` の上書き方針と最大 `content` サイズを固定する | AFT-03 | overwrite / append / create の挙動が明文化される |
| [x] | AFT-07 | `list_dir` の返却形式を `type` / `size` を含む安定 JSON に揃える | AFT-01 | LLM が path 発見に使いやすい |
| [x] | AFT-08 | file tool 共通のエラー JSON 形式を `code` / `message` / `path` ベースで揃える | AFT-05, AFT-06, AFT-07 | 拒否理由と I/O 失敗理由を機械的に区別できる |
| [x] | AFT-09 | `read_file` の bounded output / artifact 導線を `run_command` 相当に揃える | AFT-05 | 長い file でも文脈破壊せず扱える |

## M2: 検証

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | AFT-10 | host 単体テストに read/write/list の実ファイル成功系を追加する | AFT-05, AFT-06, AFT-07 | 一時ファイルで write → readback が確認できる |
| [x] | AFT-11 | host 単体テストに保護 path、相対 path、サイズ超過の拒否系を追加する | AFT-03, AFT-08 | path 境界の回帰が検知できる |
| [x] | AFT-12 | QEMU `agent_integ` に `write_file` → `read_file` 検証シナリオを追加する | AFT-06, AFT-10 | guest 上の実ファイルが正しく保存・再読込される |
| [x] | AFT-13 | QEMU `agent_integ` に保護 path 拒否 → 代替 path 成功シナリオを明示追加する | AFT-03, AFT-12 | refusal と recovery の両方を結合テストで確認できる |

## M3: 運用導線

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | AFT-14 | `/etc/CLAUDE.md` と `system_prompt.txt` に writable subtree と read-before-write を明記する | AFT-03, AFT-06 | LLM が危険 path を避けやすくなる |
| [x] | AFT-15 | `README.md` と spec 群に file tool の安全境界と検証手順を反映する | AFT-12, AFT-13 | 開発者が 1 つの説明で運用できる |

## 先送りする項目

- patch/diff 適用専用 tool
- `rm` / `mv` / `glob` / `grep` の追加
- host repo と guest filesystem をまたぐ透過 file bridge
