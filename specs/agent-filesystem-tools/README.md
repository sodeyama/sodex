# Agent Filesystem Tools Spec

agent が guest ext3 filesystem 上のファイルを安全に読み書きするための focused plan。
既存の `read_file` / `write_file` / `list_dir` を前提に、
「最低限動く」状態から「日常的に使える」状態へ引き上げる。

## 背景

現状の `agent` にはファイル系ツールがすでにある:

- `read_file`
- `write_file`
- `list_dir`

ただし、現状は以下が弱い:

- 読み書きできる path 範囲が deny-list 中心で、許可境界が曖昧
- `read_file` / `write_file` の schema と実際の制約が十分に明文化されていない
- `write_file` 成功後に実ファイルを読み戻して確認する結合テストが薄い
- 大きいファイル、相対 path、保護 path、上書き方針の扱いが不明瞭
- `/etc/CLAUDE.md` と `system_prompt.txt` に「どこへ書いてよいか」が十分に出ていない

この spec では、新しい generic tool を増やすより先に、
既存ツールを capability と validation の面で固める。

## ゴール

- agent が guest 内のファイルを narrow tool で読み書きできる
- path 解決規約、保護 path 拒否、bounded output の扱いが固定される
- `standard` mode での writable subtree が明示される
- host 単体テストと QEMU 結合テストで read/write の成功系と拒否系を継続確認できる
- prompt / config / README が同じ前提でそろう

## 非ゴール

- host 側 repo ファイルを直接読む/書く tool を guest agent に持たせること
- `rm`, `mv`, `glob`, patch 適用のような追加ツールをこの spec で一気に入れること
- freestanding guest に高度な diff editor や AST 編集器を積むこと
- 人間承認前提の対話式 permission callback を入れること

## 目標アーキテクチャ

```text
prompt / task
   │
   ▼
agent loop
   │
   ├─ tool_dispatch(read_file / write_file / list_dir)
   │
   ├─ permission / capability gate
   │    ├─ current directory 基準の path 解決
   │    ├─ read/write ごとの allow/deny prefix
   │    └─ mode=standard|strict|permissive
   │
   └─ ext3 syscall/open/read/write/getdentry
        │
        ▼
      guest filesystem
```

## Plans

| # | ファイル | 概要 | 依存 | この plan の出口 |
|---|---------|------|------|-------------------|
| 01 | [01-capability-and-path-contract.md](plans/01-capability-and-path-contract.md) | path 正規化、relative/absolute path 解決、read/write ごとの許可境界、permissions 設定形式を固める | なし | どの path を読めて、どの path に書けるかが設定とコードで一致する |
| 02 | [02-tool-io-hardening.md](plans/02-tool-io-hardening.md) | `read_file` / `write_file` / `list_dir` の入出力契約、bounded output、size 制限、エラー形式を固める | 01 | ツールの schema と実装制約が一致し、LLM が誤用しにくくなる |
| 03 | [03-validation-and-rollout.md](plans/03-validation-and-rollout.md) | host/QEMU テスト、prompt 更新、README 更新、段階的 rollout を固める | 01, 02 | 実ファイル read/write を含む主要シナリオが継続検証できる |

## マイルストーン

| マイルストーン | 対象 plan | 到達状態 |
|---------------|-----------|----------|
| M0: path 境界の固定 | 01 | 許可/拒否 path が `permissions.conf` と実装で一致する |
| M1: tool 契約の固定 | 02 | read/write/list の schema、size 制限、エラー JSON が固定される |
| M2: 検証導線の成立 | 03 | host/QEMU の両方で成功系と拒否系を継続確認できる |
| M3: 運用導線の固定 | 03 | prompt と README が file tool の安全な使い方を明記する |

## 現在の到達点

- path 解決 helper を `read_file` / `write_file` / `list_dir` / permission 判定で共通化し、relative path は current directory 基準で解決するようにした
- `permissions.conf` は read/write ごとの allow/deny prefix を持つ形式に拡張し、`standard` mode の writable subtree を `/home/user/`、`/tmp/`、`/var/agent/` に固定した
- `read_file` は `offset` / `limit` と bounded output を持ち、`write_file` は `overwrite` / `append` / `create` を持つ
- `list_dir` は `name` / `path` / `type` / `size` を返す安定 JSON に揃えた
- file tool 共通のエラー JSON を導入し、permission block と I/O failure を区別できるようにした
- host 単体テストと QEMU `agent_integ` に write → readback、保護 path 拒否 → home への recovery を追加した
- `system_prompt.txt`、`/etc/CLAUDE.md`、repo `README.md` に安全境界を反映した

focused plan として切った項目は、現時点では実装と検証まで完了している。

## 実装順序

1. `permissions.conf` と `permissions.c` の契約を先に固定する
2. `read_file` / `write_file` / `list_dir` の schema と返却 JSON を揃える
3. host 単体テストで path 境界と I/O 契約を固定する
4. QEMU 結合テストで実ファイルの write → readback を確認する
5. `/etc/CLAUDE.md`、`system_prompt.txt`、README に運用ルールを反映する

## 主な設計判断

- host repo ではなく guest ext3 filesystem を対象とする
- absolute path と current directory 基準の relative path を許可する
- `standard` mode では read を広く、write を狭くする
- `write_file` は deny-list だけに頼らず writable prefix を持つ
- 大きい read 結果は `run_command` と同様に bounded output 前提で扱う
- 成功判定は「write が返った」だけでなく「readback で一致した」まで含める

## 関連 spec / 参考

- `specs/agent-transport/README.md`
- `specs/agent-transport/plans/12-tool-execution.md`
- `specs/agent-transport/plans/15-system-prompt-and-tools.md`
- `specs/agent-transport/plans/17-hooks-and-permissions.md`
- `specs/agent-transport/plans/18-agent-integration-test.md`
