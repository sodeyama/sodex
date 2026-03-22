# Sodex 内部で runtime を作り interpreter 的な言語を動かす案の調査

**調査日**: 2026-03-22  
**目的**: compiler 型ではなく、Sodex 内部に runtime を持ち、source または bytecode を解釈実行する言語環境が現実的かを評価する

---

## 1. 結論

### 結論

**interpreter 型の言語環境は十分ありえる。むしろ初期段階では compiler 型より着手しやすい。**

ただし、初手で Lua/Wren/Wasm runtime をそのまま移植するよりも、**Sodex 専用の小さい言語を source interpreter として入れ、必要なら後で bytecode VM に進化させる形**が最も現実的である。

### 推奨順位

1. **Sodex 専用の source interpreter**
2. **同じ言語の bytecode VM 化**
3. **必要なら後で AOT compiler を追加**

---

## 2. なぜ interpreter 案が成立するか

Sodex には、interpreter を guest 内で回すための前提が既にかなり揃っている。

### 2.1 実行・編集・保存の基盤

- rich terminal、`vi`、UTF-8、日本語 IME がある
- ext3 に file を保存できる
- shell script 実行がある
- `agent` が file read/write と command 実行を持つ  
  参考: [`README.md`](../../README.md)

### 2.2 agent との相性

interpreter 案は agent と相性が良い。

- `write_file` は 1 回 3072 bytes 上限なので、巨大 source の一括生成には向かない
- `run_command` は約 10 秒 timeout があるので、重い compile ループは不利
- interpreter なら `save -> run` で済み、待ち時間を減らせる  
  参考: [`src/usr/lib/libagent/tool_write_file.c`](../../src/usr/lib/libagent/tool_write_file.c), [`src/usr/lib/libagent/tool_run_command.c`](../../src/usr/lib/libagent/tool_run_command.c)

### 2.3 メモリ余裕

- 既定 512MB
- 最大 1GB まで使える
- userland `malloc/brk` 回帰も確認済み

小さめの VM や interpreter を載せるには十分な headroom がある。  
参考: [`specs/memory-scaling/README.md`](../../specs/memory-scaling/README.md)

---

## 3. 既存 shell をそのまま言語にする案

### 3.1 できること

Sodex の `sh` / `eshell` は既に interpreter であり、次を持つ。

- script 実行
- `if`
- `for`
- `while`
- pipeline
- redirection

参考: [`src/usr/include/shell.h`](../../src/usr/include/shell.h), [`src/usr/lib/libc/shell_script.c`](../../src/usr/lib/libc/shell_script.c)

### 3.2 限界

ただし、一般的な agent 用プログラミング言語としては厳しい。

- `SHELL_STORAGE_SIZE=8192`
- `SHELL_MAX_ARGS=24`
- `SHELL_MAX_NODES=64`
- 値型が実質的に文字列中心
- 配列、map、構造体、関数値、モジュールなどが弱い

つまり shell は「運用 glue」としては良いが、「agent が複雑なロジックを書く主言語」には向かない。

---

## 4. interpreter 方式の選択肢

### 4.1 Tree-walk interpreter

#### 利点

- 実装が最小
- parser の次にすぐ動く
- 診断を作りやすい
- REPL を作りやすい

#### 欠点

- 実行速度が遅い
- 長い loop や text processing で不利

#### Sodex との相性

**初手の MVP として最適**。  
agent の生成・実行ループを確認するには十分。

### 4.2 Bytecode VM

#### 利点

- tree-walk より速い
- source と runtime を分離しやすい
- 後で AOT compiler にも繋げやすい
- VM instruction 単位で sandbox を作りやすい

#### 欠点

- ディスパッチ loop、bytecode format、constant pool、call frame などが必要
- source interpreter より実装量が増える

#### Sodex との相性

**中期の本命**。  
最終的に目指すならこれが一番バランスが良い。

### 4.3 既存 embeddable interpreter を移植

候補はある。

- Lua
- Wren
- WebAssembly runtime

ただし「Sodex 内で agent が自前コードを書いてすぐ動かす」用途では、それぞれ別の重さがある。

---

## 5. 既存 runtime 候補の比較

| 候補 | 向いている用途 | Sodex での課題 | 初手の適性 |
|---|---|---|---|
| Lua | 汎用 scripting、REPL、埋め込み | GC と標準ライブラリ移植、`io`/`os` の整備が必要 | 中 |
| Wren | 埋め込み VM、モジュール化された script | GC、fiber、object model がやや重い | 中 |
| WAMR | sandbox 実行、将来の外部 module 実行 | Wasm 生成パイプラインが別途必要 | 低 |
| 独自 source interpreter | agent-native な軽量 script | 言語仕様を自前で決める必要がある | **最良** |
| 独自 bytecode VM | 実用 runtime | source interpreter より実装量が多い | 高 |

---

## 6. 外部 runtime の調査結果

### 6.1 Lua

Lua 公式 manual では、standalone interpreter は **textual source** と **precompiled binary** の両方を実行でき、interactive mode も持つ。  
また公式 reference manual は language、standard libraries、C API を定義している。

参考:

- <https://www.lua.org/manual/5.4/lua.html>
- <https://www.lua.org/manual/5.4/>

#### Sodex への評価

Lua 自体は有力だが、Sodex にそのまま持ち込むと次が論点になる。

- automatic GC を前提にする
- `io` / `os` / module loading の実装方針を決める必要がある
- freestanding 気味の libc に合わせて porting 差分が出る

**結論**: 可能だが、初手から Lua 移植をやるのは少し重い。

### 6.2 Wren

Wren 公式 embedding 文書では次が明記されている。

- source を `wrenInterpret()` で実行する
- 実行前に source を bytecode に compile する
- source 直埋め込みで使える
- C99 として compile しやすい
- C standard library 以外にほぼ依存しない
- GC と handle 管理が必要

参考:

- <https://wren.io/embedding/>

#### Sodex への評価

Lua より「埋め込み VM」としての形は見えやすい。  
一方で object model、fiber、GC を含むので、Sodex で欲しい最小 runtime としてはやや豪華である。

**結論**: 既存 runtime を選ぶなら Lua より Wren の方が方向性は近いが、やはり初手としては少し重い。

### 6.3 WebAssembly Micro Runtime (WAMR)

WAMR は公式 README で次を掲げている。

- lightweight standalone Wasm runtime
- interpreter / AOT / JIT を持つ
- built-in libc subset または WASI を選べる
- embed 用 C API を持つ

参考:

- <https://github.com/bytecodealliance/wasm-micro-runtime>

#### Sodex への評価

Wasm runtime は sandbox と module ABI の観点では非常に魅力的である。  
ただし今回の目的は「guest 内で agent が言語を使ってすぐ走らせること」であり、Wasm runtime だけでは足りない。

別途必要になるもの:

- Wasm frontend
- Wasm module を生成する toolchain
- WASI や native API binding の設計

**結論**: 将来の module sandbox には良いが、最初の guest-native 言語としては遠回り。

---

## 7. Sodex に最も合う runtime 形

### 7.1 推奨

**独自言語 `sx` をまず source interpreter `sxi` として実装する。**

形は次の通り。

- `/usr/bin/sxi`
- source file を直接読む
- `sxi script.sx`
- `sxi -e '...'`
- `sxi` 単体で REPL

### 7.2 v0 で持つべきもの

- `let`
- `if`
- `while`
- `fn`
- `return`
- `i32`
- `bool`
- `str`
- byte buffer
- array
- map
- file API
- process / command API の最小 wrapper

### 7.3 最初は入れないもの

- class
- inheritance
- GC 前提の複雑な object graph
- closure
- module loader 完全版
- threads

---

## 8. runtime 実装方式の推奨

### 8.1 v0: source interpreter

内部構成:

1. lexer
2. parser
3. AST
4. tree-walk evaluator

#### 理由

- 実装が一番軽い
- エラーメッセージを出しやすい
- host unit test を書きやすい
- `agent` の修正ループに向く

### 8.2 v1: bytecode VM

source interpreter と同じ frontend を使って、backend だけ bytecode へ差し替える。

内部構成:

1. lexer
2. parser
3. AST または直接 IR
4. bytecode emitter
5. VM loop

#### 理由

- 実行速度を上げられる
- 将来 `sxc` に繋げやすい
- module cache を作りやすい

### 8.3 v2: optional compiler

必要になったら同じ frontend から AOT compiler を追加する。  
この順なら language 設計と runtime 設計を先に固められる。

---

## 9. メモリ管理の勧め

### 9.1 初手で GC を入れない案

初手は **arena + 明示寿命** でもよい。

- 1 script 実行ごとに arena を作る
- script 終了でまとめて破棄
- REPL は command 単位で reset

これなら GC 実装なしで早く進められる。

### 9.2 GC が必要になる地点

次が欲しくなったら GC を検討する。

- 長寿命 object
- REPL での値保持
- closure
- module cache
- object graph

その時点で stop-the-world mark-sweep を入れれば十分で、generational GC までは不要。

---

## 10. compiler 案との関係

前回の調査では compiler 型を推奨したが、今回の調査で補正すると次の整理になる。

### 10.1 初手

**interpreter が有利**

理由:

- compile step がない
- agent の timeout 制約に強い
- REPL を作れる
- 小さい source をすぐ試せる

### 10.2 中長期

**同じ言語で compiler も持つ形が最良**

つまり対立ではなく、次の順が自然である。

1. `sxi` source interpreter
2. `sxb` bytecode VM
3. `sxc` native compiler

---

## 11. 最終提案

Sodex では compiler 型だけでなく、**runtime を持つ interpreter 的な言語環境は十分に成立する**。  
むしろ初期段階ではこちらの方が agent との相性が良い。

最も筋が良いのは次の形である。

- shell は glue 用に残す
- 新しい主言語は独自の `sx`
- 最初は `sxi` source interpreter
- 後で `sxb` bytecode VM
- 最後に必要なら `sxc` compiler

要するに、**いきなりコンパイラを完成させるより、まず runtime を作って言語の使い勝手と agent ループを固める方がよい。**

---

## 12. 次にやるべきこと

1. `sx` の v0 文法を 1 ページに固定する
2. `sxi` の最小 runtime API を決める
3. `hello`, file copy, grep-lite, JSON parse の 4 本を最初の目標にする
4. host unit test で parser/evaluator を先に回す
5. QEMU smoke で `agent -> write_file -> sxi run -> 修正` ループを固定する

