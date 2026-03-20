# Plan 23: Unix 系 text command 群と agent からの活用

## 概要

Plan 10, 11, 16, 22 により、guest 内で file 操作、pipe、redirection、pager の基盤は揃いつつある。
ただし日常的な調査・加工・比較を guest だけで閉じるには、Unix 系の text command 群がまだ大きく不足している。

- tree を辿る `find`
- line を並べ替える `sort`
- 隣接重複を扱う `uniq`
- 件数確認の `wc`
- 先頭 / 末尾を見る `head` / `tail`
- 検索の `grep`
- stream 編集の `sed`
- field 指向処理の `awk`
- 列抽出の `cut`
- 文字変換の `tr`
- 差分確認の `diff`
- pipe を分岐する `tee`

これらがないと、agent の `run_command` も guest 内の加工を細かく shell script へ埋め込むしかなく、
shell / agent のどちらから見ても再利用性が低い。

この plan では、POSIX/BSD の代表的な引数を調査した上で、
guest 内で実用になる初期 subset を定義し、`/usr/bin/*` に command を追加する。
agent については `run_command` が `/usr/bin/sh -c ...` を使うため、
バイナリを image へ収録し、agent 側の既定文脈でも使える command 群として明示する。

## 依存と出口

- 依存: 11, 13, 16
- この plan の出口
  - `/usr/bin/find`, `sort`, `uniq`, `wc`, `head`, `tail`, `grep`, `sed`, `awk`, `cut`, `tr`, `diff`, `tee` が guest 内で動く
  - shell の pipe / redirection と組み合わせた基本 workflow が成立する
  - agent の `run_command` からも追加 command 群を前提に使える
  - host test と QEMU smoke で代表的な加工・比較フローを回帰検知できる

## 調査結果と採用する初期 subset

手元の `man` を基準に、BSD/POSIX で共通性が高く、guest 実装の規模に対して効果が大きい引数を初期 subset とする。

| command | 調査した代表引数 | Plan 23 で実装する引数 |
|---|---|---|
| `find` | `-name`, `-type`, `-maxdepth`, `-mindepth`, `-print` | `-name`, `-type`, `-maxdepth`, `-mindepth`、暗黙 `-print` |
| `sort` | `-n`, `-r`, `-u`, `-o`, `-t`, `-k` | `-n`, `-r`, `-u`, `-o`, `-t`, `-k` |
| `uniq` | `-c`, `-d`, `-u`, `-f`, `-s` | `-c`, `-d`, `-u`, `-f`, `-s` |
| `wc` | `-l`, `-w`, `-c`, `-m` | `-l`, `-w`, `-c` |
| `head` | `-n`, `-c` | `-n`, `-c` |
| `tail` | `-n`, `-c`, `-f`, `-r` | `-n`, `-c` |
| `grep` | `-F`, `-i`, `-v`, `-n`, `-c`, `-q`, `-e`, `-f` | `-F`, `-i`, `-v`, `-n`, `-c`, `-q`, `-e` |
| `sed` | `-n`, `-e`, `-f`, `s///`, `p`, `d`, `q` | `-n`, `-e`, `s///`, `p`, `d`, `q` |
| `awk` | `-F`, `-v`, `BEGIN`, `END`, `print`, `NF`, `NR` | `-F`, `-v`, `BEGIN`, `END`, `print`, `$0`, `$N`, `NF`, `NR` |
| `cut` | `-b`, `-c`, `-f`, `-d`, `-s` | `-c`, `-f`, `-d`, `-s` |
| `tr` | `-d`, `-s`, `-c` | 変換、`-d`, `-s`, `-c` |
| `diff` | `-q`, `-u`, directory diff | `-q`, `-u` |
| `tee` | `-a`, `-i` | `-a` |

## 意図的に切る範囲

- 初期 `grep` は back reference や複雑な正規表現までは扱わない
  - `^`, `$`, `.`, `*` を含む基本 regex と `-F` の fixed string を優先する
- 初期 `sed` は script file (`-f`) と in-place (`-i`) を扱わない
- 初期 `awk` は full awk ではなく、`print` 中心の pattern-action subset とする
  - 算術、連想配列、ユーザー関数、`printf`、`next` は後回し
- 初期 `cut` は byte list (`-b`) を省き、char と field に絞る
- 初期 `tail` は follow mode (`-f`) と reverse (`-r`) を省く
- 初期 `diff` は directory tree 再帰比較を扱わない
- 初期 `tee` は `-i` を省く

## 方針

- command ごとに独立実装を増やしすぎず、共通の text-tool helper を userland libc に切り出す
- pure logic を host test できる構造を優先する
  - line split
  - range/list parse
  - wildcard / basic regex
  - sort key compare
  - diff algorithm
  - `awk` / `sed` の最小 parser
- guest 依存部分は薄く保つ
  - file / stdin 読み込み
  - directory walk
  - stdout / stderr への出力
- UTF-8 環境ではあるが、Plan 23 の初期 text command は基本的に byte / line 中心で揃える
  - `cut -c` と `awk` の field split は UTF-8 を壊さないようにする
  - `tr` は 1 byte 文字集合を対象とした初期実装に留める

## 設計判断

- `find`
  - shell expansion ではなく command 自身が directory tree を辿る
  - `-name` は shell style wildcard (`*`, `?`) を使う
  - `-type` は `f`, `d` に絞る
- `sort`
  - file 全体を読み込み、line 配列へ分解して sort する
  - `-k` は 1-origin field index とし、`-t` 指定時は単一 delimiter を使う
- `uniq`
  - `sort` 済み前提ではなく、「隣接重複を扱う」本来の意味を守る
- `grep`
  - default は basic regex subset、`-F` は fixed string
  - 複数 `-e` は OR で扱う
- `sed`
  - command は `-e` または最初の非 option 引数から受け取る
  - command 列は `;` で連結できる
  - address はなし / 単一行番号 / `$` に絞る
- `awk`
  - `BEGIN`, 通常行, `END` の 3 相に分ける
  - action は `print` のみ
  - expression は string literal, `$0`, `$N`, `NF`, `NR`, 変数参照の最小 subset とする
- `diff`
  - line 単位 LCS を使う
  - `-q` は一致/不一致のみ、`-u` は unified diff を出す
- agent
  - `run_command` の挙動はそのまま使い、追加 command 群を `/usr/bin` へ入れる
  - agent 既定 prompt / system info に text command 群の存在を足して discoverability を上げる

## 実装ステップ

1. `unix_text_tools` 系の共通 helper を追加する
2. line 入力、line 配列、range/list parse、wildcard、basic regex の pure logic を host test で固定する
3. `find`, `sort`, `uniq`, `wc`, `head`, `tail` を追加する
4. `grep`, `cut`, `tr`, `tee` を追加する
5. `sed` の最小 script engine を追加する
6. `awk` の最小 pattern-action engine を追加する
7. `diff` の line diff を追加する
8. `src/usr/command/makefile` と image 収録を更新する
9. agent 既定 prompt / system info に command 群の存在を反映する
10. host test と QEMU smoke を追加する
11. README / spec の command 一覧を更新する

## 変更対象

- 既存
  - `src/usr/command/makefile`
  - `src/usr/lib/libagent/agent_loop.c`
  - `src/usr/lib/libagent/tool_get_system_info.c`
  - `tests/Makefile`
  - `README.md`
  - `specs/rich-terminal/README.md`
  - `specs/rich-terminal/TASKS.md`
- 新規候補
  - `src/usr/include/unix_text_tools.h`
  - `src/usr/lib/libc/unix_text_tools.c`
  - `src/usr/command/find.c`
  - `src/usr/command/sort.c`
  - `src/usr/command/uniq.c`
  - `src/usr/command/wc.c`
  - `src/usr/command/head.c`
  - `src/usr/command/tail.c`
  - `src/usr/command/grep.c`
  - `src/usr/command/sed.c`
  - `src/usr/command/awk.c`
  - `src/usr/command/cut.c`
  - `src/usr/command/tr.c`
  - `src/usr/command/diff.c`
  - `src/usr/command/tee.c`
  - `tests/test_unix_text_tools.c`
  - `src/test/run_qemu_unix_text_tools_smoke.py`

## 検証

- `find . -type f -name "*.txt"` が tree walk できる
- `sort -n`, `sort -u`, `sort -t : -k 2` が期待順で出る
- `uniq -c`, `uniq -d`, `uniq -u` が隣接重複に対して正しく動く
- `wc -lwc`, `head -n`, `tail -n`, `tail -c` が file / stdin の両方で動く
- `grep -n`, `grep -v`, `grep -c`, `grep -q`, `grep -F` が pipe でも file でも動く
- `sed -n -e '1p' -e 's/foo/bar/g'` が意図通りに変換できる
- `awk -F : '{ print $1, $3 }'` と `END { print NR }` が動く
- `cut -d : -f 1,3`, `cut -c 1-5`, `tr -d`, `tr -s`, `tee -a` が pipe 内で動く
- `diff -q` と `diff -u` が line 差分を見られる
- agent が `run_command` でこれらの command 名を前提に使える

## 完了条件

- guest 内で「探す・絞る・数える・先頭末尾を見る・変換する・比較する・分岐する」の基本 workflow が揃う
- shell script と agent の両方で再利用できる text processing 基盤になる
