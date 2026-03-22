# Sodex 内部で agent が自律的にプログラムを作成・コンパイルする環境の調査

**調査日**: 2026-03-22  
**目的**: Sodex 内で agent がソース生成、保存、コンパイル、実行、再実行まで閉じられる開発環境の形を比較し、最初に採るべき構成を決める

---

## 1. 結論

### 推奨方針

**Sodex 専用の小さい新言語を作り、guest 内で動く native compiler を載せる構成**を推奨する。

初手では GCC/TCC 級の既存 C compiler 移植よりも、次の条件に最適化した専用系の方が現実的である。

- i486 向け
- static な 32bit ELF 実行ファイルを直接出力
- Sodex の薄い userland libc に合わせる
- agent が短時間で繰り返し回せるコンパイル速度を優先
- agent 向けに機械可読な診断を返せる

### 非推奨

- 初手から TCC を移植する
- 初手から C 互換を強く目指す
- まず VM 言語を載せて、その上で native 実行を後回しにする

---

## 2. 現状の Sodex で既に揃っているもの

Sodex には、guest 内開発環境の前段として必要な要素がかなり揃っている。

### 2.1 実行基盤

- カーネルは `execve` と ELF loader を持つ  
  参考: [`README.md`](../../README.md), [`src/elfloader.c`](../../src/elfloader.c), [`src/execve.c`](../../src/execve.c)
- userland 実行形式は static link の 32bit ELF  
  参考: [`src/usr/makefile.inc`](../../src/usr/makefile.inc)
- `crt0` は最小で、`_start -> main -> exit` の形  
  参考: [`src/usr/lib/crt0.S`](../../src/usr/lib/crt0.S)

### 2.2 編集・保存・実行

- rich terminal、`vi`、UTF-8、日本語 IME が実用段階
- ext3 で `/home/user` にファイル保存可能
- shell は script 実行、`if/for/while`、pipe、redirection を持つ
- `./prog` のような slash 付き path もそのまま実行できる  
  参考: [`src/usr/include/shell.h`](../../src/usr/include/shell.h), [`src/usr/lib/libc/shell_executor.c`](../../src/usr/lib/libc/shell_executor.c)

### 2.3 agent 側の実用基盤

- `agent` は `read_file`, `write_file`, `rename_path`, `list_dir`, `run_command` を持つ
- `run_command` は `sh -c ...` を child として起動して stdout/stderr を回収できる
- `write_file` は `standard` mode で `/home/user`, `/tmp`, `/var/agent` に書ける
- `write_file` 1 回あたりの content 上限は 3072 bytes
- `run_command` には約 10 秒相当の timeout がある  
  参考: [`README.md`](../../README.md), [`src/usr/lib/libagent/tool_run_command.c`](../../src/usr/lib/libagent/tool_run_command.c), [`src/usr/lib/libagent/tool_write_file.c`](../../src/usr/lib/libagent/tool_write_file.c)

### 2.4 メモリ headroom

- 既定 QEMU RAM は 512MB
- kernel direct map は最大 1GB
- userland `malloc/brk` 回帰も高メモリ構成で確認済み  
  参考: [`specs/memory-scaling/README.md`](../../specs/memory-scaling/README.md)

---

## 3. 制約

この repo の guest userland 向け compiler を考える際の強い制約は次の通り。

### 3.1 libc が薄い

Sodex userland libc は `printf`, `snprintf`, `malloc`, `open/read/write/close`, `execve` などの基本はあるが、一般的な hosted 環境ほど厚くない。  
大きい既存 compiler の移植先としてはやや不利である。  
参考: [`src/usr/include/stdio.h`](../../src/usr/include/stdio.h), [`src/usr/include/stdlib.h`](../../src/usr/include/stdlib.h)

### 3.2 ELF loader は単純

逆にこれは利点でもある。  
loader は static な ELF を読み、program header ごとに page を張って配置する素直な形なので、**固定レイアウトの実行可能 ELF を直接吐く compiler** と相性が良い。  
参考: [`src/elfloader.c`](../../src/elfloader.c)

### 3.3 agent の編集 I/O に上限がある

- `write_file` は 3072 bytes 制限
- `run_command` は約 10 秒 timeout

つまり agent が毎回巨大な source を 1 ファイル生成し、重い compiler を何十秒も回す形は不向きである。

---

## 4. 選択肢の比較

| 方式 | 利点 | 欠点 | 初手の適性 |
|---|---|---|---|
| Sodex 専用新言語 + native AOT compiler | Sodex に最適化できる、ELF 直出ししやすい、診断を機械可読化しやすい、小さく保てる | 言語仕様と compiler を自前で作る必要がある | **最良** |
| Lua/Wren 系 VM | REPL と埋め込みがしやすい、実装例が多い | 成果物が native command になりにくい、GC や VM API の導入が必要 | 次点 |
| TCC など既存 C compiler 移植 | C に近い体験を得やすい、既存知見が多い | libc 依存が重い、移植面積が大きい、debug が重い | 初手では不向き |

---

## 5. 推奨アーキテクチャ

### 5.1 形

以下の 3 層に分ける。

1. **言語 `sx`**
2. **compiler `sxc`**
3. **最小 runtime `sxrt`**

### 5.2 配置イメージ

- `/usr/bin/sxc`
- `/usr/include/sx/` または `/usr/lib/sx/`
- `/home/user/src/*.sx`
- `/home/user/bin/*`

### 5.3 コンパイル結果

`sxc` は次を直接出力する。

- static ELF32 executable
- entry は既存 `crt0` に合わせて `main`
- link は可能なら内部完結
- 最初は relocation や shared library を扱わない

### 5.4 compiler の内部段階

最初は以下で十分。

1. lexer
2. parser
3. 型検査の最小 subset
4. 単純 IR
5. i386 codegen
6. ELF writer

**重要**: 初手では assembler や外部 linker に逃がさず、`sxc` 単体で固定レイアウト ELF を吐く方がよい。  
Sodex の ELF loader が素直なので成立可能性が高い。

---

## 6. 言語設計の方向

### 6.1 C 風だが C 互換ではない言語

最初は「見た目だけ少し C に近い」程度で十分。

例:

```txt
fn main() -> i32 {
  let fd: i32 = open("/tmp/hello.txt", O_CREAT | O_TRUNC | O_WRONLY, 0644)
  write(fd, "hello\n")
  close(fd)
  return 0
}
```

### 6.2 v0 の最小要素

- `fn`
- `let`
- `if`
- `while`
- `return`
- `i32`
- `bool`
- `str`
- 固定長 buffer
- extern import で少数の syscall wrapper を呼ぶ

### 6.3 初手で入れないもの

- GC
- class / object
- closure
- module system の完全版
- 浮動小数点
- generics
- 例外

---

## 7. 既存言語を初手にしない理由

### 7.1 TCC

TCC は「小さい compiler / assembler / linker をまとめて持つ」という意味では方向性が近い。  
ただし Sodex userland libc は hosted 環境ではなく、移植時の差分吸収コストが高い。

また、今回の目的は「C 互換 compiler を動かすこと」ではなく、**agent が自律的に短い生成・実行ループを回せること**なので、最適化対象がずれる。

### 7.2 Lua / Wren

Lua や Wren は埋め込み言語としては有力である。

- Lua は standalone interpreter と precompiled binary を持つ
- Wren は組み込み VM として小さく、C 標準ライブラリ依存だけで組み込みやすい

ただし今回欲しいのは「guest 内で普通の実行ファイルを作る環境」であり、VM 言語はそこに直結しない。  
将来的に REPL 用サブシステムとしては有力だが、**最初の主軸にはしない方がよい**。

外部参考:

- Lua manual: <https://www.lua.org/manual/5.4/lua.html>
- Wren embedding: <https://wren.io/embedding>
- TCC/TCCBOOT: <https://bellard.org/tcc/tccboot.html>

---

## 8. 段階導入案

### Phase 0: shell ベースの暫定運用

今すぐでも agent は shell script を生成して `sh` で回せる。  
ただしこれは「コンパイル環境」ではなく、あくまで暫定策。

### Phase 1: host build の `sxc` を guest に載せる

最初の `sxc` 自体は host 上の既存 cross build で作り、image に収録する。  
guest 内では `sxc` 実行だけを行う。

### Phase 2: agent と compiler の接続

agent 向けに次を整える。

- `sxc check --json`
- `sxc build input.sx -o out`
- `sxc run input.sx`

特に `--json` は重要で、診断が構造化されていれば agent が自律修正しやすい。

### Phase 3: self-host subset

compiler の一部または compiler-support tool を `sx` で書き直し、自己拡張可能にする。  
ただし self-host は初手のゴールにしない。

---

## 9. agent 利用を前提にした設計上の注意

### 9.1 1 ファイル巨大主義を避ける

`write_file` 制限の都合で、agent が一度に巨大 source を吐く設計は扱いにくい。  
小さい module 群で構成する方がよい。

### 9.2 診断は人間向け文字列だけにしない

最低限次の JSON を返せるようにしたい。

- file
- line
- column
- severity
- code
- message

### 9.3 compiler 時間は短く抑える

agent の `run_command` timeout があるので、初手は数百 ms から数秒で終わる規模を狙うべきである。

---

## 10. 最終提案

Sodex に欲しいのは「汎用 UNIX 開発環境」ではなく、**agent が guest 内で自律的にプログラムを作って直ちに走らせる環境**である。

その目的に対して最も筋が良いのは次の構成である。

- Sodex 専用の小さい新言語を作る
- guest 内で動く `sxc` を用意する
- `sxc` は static i386 ELF を直接出力する
- runtime は最小 syscall wrapper 中心にする
- agent 向けに JSON 診断と短時間ビルドを重視する

**要するに、C 互換環境を guest に再現するのではなく、Sodex 自体を agent-native な開発対象に寄せる方がよい。**

---

## 11. 次にやるべきこと

1. `specs/` に guest-native compiler 向けの新 spec を作る
2. `sx` v0 の文法と型を 1 ページに固定する
3. `sxc` の出力を「固定レイアウト ELF32」に限定して MVP を作る
4. `hello`, `cat`, `cp`, `grep-lite` 相当の小課題で agent ループを検証する

