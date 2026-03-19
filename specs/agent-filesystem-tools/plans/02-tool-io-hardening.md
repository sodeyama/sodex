# Plan 02: File Tool の I/O Hardening

## 目的

`read_file` / `write_file` / `list_dir` の schema、サイズ制限、返却 JSON、
bounded output の扱いを揃え、LLM が誤用しにくい narrow tool にする。

## 現状の問題

- `read_file` は全文読み前提で、分割取得の契約が弱い
- `write_file` は overwrite 前提だが、仕様として十分に固定されていない
- `content` 上限が実装依存で、仕様書と prompt に出ていない
- 成功/失敗レスポンスの JSON 形が tool ごとに揺れやすい
- `list_dir` は path 発見用途としての返却契約をより明示したい

## 方針

### `read_file`

- `path` を必須
- 必要なら `offset`, `limit` を追加
- 長い本文は bounded output + artifact で返す
- `bytes_read`, `truncated`, `artifact_path` を返せる形にする

### `write_file`

- `path`, `content` を必須
- 既定は `overwrite`
- 必要なら `mode=create|overwrite|append` を追加する
- `content` の最大サイズを固定する
- `bytes_written` と最終 path を返す

### `list_dir`

- entry ごとに `name`, `path`, `type`, `size` を返す
- 必要なら件数上限を持つ
- LLM が次の `read_file` にそのまま使える absolute path を返す

### 共通エラー形式

最低限以下を揃える:

```json
{
  "error": {
    "code": "permission_denied",
    "message": "write to protected path is not allowed",
    "path": "/boot/blocked.txt"
  }
}
```

`invalid_path`, `permission_denied`, `not_found`, `io_error`, `too_large`
をまず固定する。

## 変更対象

- `src/usr/lib/libagent/tool_read_file.c`
- `src/usr/lib/libagent/tool_write_file.c`
- `src/usr/lib/libagent/tool_list_dir.c`
- `src/usr/include/agent/tool_handlers.h`
- `src/usr/lib/libagent/tool_init.c`
- `src/rootfs-overlay/etc/agent/system_prompt.txt`

## テスト

### host 単体

- 小さい file の read 成功
- `offset` / `limit` 付き read 成功
- サイズ超過 write の拒否
- overwrite と append の契約確認
- 空ディレクトリと複数 entry の list 成功

### QEMU

- `write_file(/tmp/x)` → `read_file(/tmp/x)` で内容一致
- 大きい read が bounded output になる
- 不正 path で統一エラー JSON が返る

## 完了条件

- schema と実装制約が一致する
- file tool の成功/失敗レスポンスが安定 JSON になる
- LLM が `list_dir` → `read_file` / `write_file` を迷いなく連鎖できる
