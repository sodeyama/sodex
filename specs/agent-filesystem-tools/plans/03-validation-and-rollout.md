# Plan 03: Validation と Rollout

## 目的

file tool を仕様書だけで終わらせず、
host 単体テスト、QEMU 結合テスト、prompt、README を揃えて
継続運用できる状態にする。

## 方針

### 1. host 単体テストを増やす

狙い:

- permission 境界を速く回せること
- 実ファイルの write → readback を QEMU 前に固定できること

追加候補:

- `tests/test_tool_file_access.c`
- 既存 `tests/test_hooks_permissions.c` の拡張

### 2. QEMU `agent_integ` を強化する

既存の permission recovery シナリオは残しつつ、
「本当にファイルが書けたか」を確認するシナリオを追加する。

最低限ほしいケース:

- one write + readback
- protected path blocked + retry
- relative path resolved from current directory

### 3. prompt / config / README を揃える

更新対象:

- `/etc/CLAUDE.md`
- `/etc/agent/system_prompt.txt`
- `README.md`

明記したい内容:

- relative path は current directory 基準で解決される
- 書く前に読む
- `standard` mode の writable subtree
- 大きい file は分割取得する

### 4. rollout は小さく進める

順序:

1. host 単体テストを追加
2. `permissions.conf` と tool 実装を更新
3. QEMU `agent_integ` を更新
4. prompt / README を更新

## 変更対象

- `tests/Makefile`
- `tests/test_hooks_permissions.c`
- `tests/test_tool_dispatch.c`
- `src/usr/command/agent_integ.c`
- `tests/mock_claude_server.py`
- `tests/run_agent_integration.py`
- `src/rootfs-overlay/etc/CLAUDE.md`
- `src/rootfs-overlay/etc/agent/system_prompt.txt`
- `README.md`

## 完了条件

- host 単体テストで file tool の成功系と拒否系が固定される
- `make test-agent-full` 相当で write → readback が確認できる
- prompt と README が file tool の安全境界を明示する
- 実装・テスト・文書の前提が食い違わない
