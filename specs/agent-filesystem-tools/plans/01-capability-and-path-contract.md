# Plan 01: Capability と Path 契約

## 目的

agent の file tool が「どの path を読めて、どの path に書けるか」を
設定ファイル、実装、prompt の 3 箇所で一致させる。

## 現状の問題

- `write_file` の拒否は主に deny-list で、writable subtree が明示されていない
- hook と permission が両方 path を見ており、責務が分散している
- relative path や `..` を含む path の扱いが仕様として固定されていない
- `read_file` / `list_dir` の read 境界も文書化が弱い

## 方針

### 1. path 解決規約を固定する

- absolute path はそのまま正規化する
- relative path は current directory から解決する
- 既定起動位置は `/home/user` とする
- エラーは `invalid_path` 系で返す

### 2. path 正規化を共通 helper に寄せる

最低限以下を統一する:

- `//` の圧縮
- `/./` の除去
- `..` の解決
- 末尾 `/` の扱い

permission、hook、tool 実装がそれぞれ文字列検索する状態をやめる。

### 3. read と write の境界を分ける

`standard` mode の基本方針:

- read: 広く許可
- write: 狭く許可
- deny: `/boot/`, `/etc/agent/` は常時保護

初期 writable subtree の候補:

- `/home/user/`
- `/tmp/`
- `/var/agent/`
- 必要なら config で追加した subtree

### 4. `permissions.conf` を allow/deny prefix 中心にする

例:

```text
mode=standard
read_allow=/
read_deny=/etc/agent/
write_allow=/tmp/
write_allow=/home/user/
write_allow=/var/agent/
write_deny=/boot/
write_deny=/etc/agent/
deny_cmd=rm -rf
deny_cmd=dd if=
deny_cmd=mkfs
```

初手では glob ではなく prefix match に寄せる。
小さい実装で説明可能にするため。

## 変更対象

- `src/usr/include/agent/permissions.h`
- `src/usr/lib/libagent/permissions.c`
- `src/usr/lib/libagent/agent_loop.c`
- `src/rootfs-overlay/etc/agent/permissions.conf`
- `tests/test_hooks_permissions.c`

## テスト

### host 単体

- relative path が current directory から解決される
- `/boot/x` への write を拒否
- `/home/user/x` への write を許可
- `/tmp/x` への write を許可
- `/etc/agent/x` の read/write を policy 通りに判定
- `a/../b` のような path が正規化される

### QEMU

- relative path で home 配下への write が成功
- `/boot/blocked.txt` への write が拒否
- 拒否後に代替 path へ書き直せる

## 完了条件

- path 判定が deny-list の断片ロジックではなく、共通 helper + policy で説明できる
- `standard` mode で writable subtree が明文化される
- protected path への write が host/QEMU の両方で回帰防止される
