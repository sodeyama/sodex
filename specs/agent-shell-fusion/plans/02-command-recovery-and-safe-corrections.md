# Plan 02: command recovery と safe correction

## 概要

router の次に必要なのは、
command-not-found や typo を agent 以前の層で安全に吸収することだ。

既存調査でも、ここは suggestion 型が主流であり、
無言での自動実行は destructive risk が高い。

本 Plan では、非 LLM の高信頼補正を先に作り、
必要な場合だけ console error recovery と LLM fallback を載せる。

## 初期 scope

- command-not-found 時に recovery path を起動する
- 実行ファイル名 typo の候補を出す
- path typo、history 近傍を候補化する
- 代表的 console error から修正案を出す
- destructive command は auto-apply しない

## 非ゴール

- freeform 自然言語から複雑 command を毎回生成すること
- shell parser を飛ばして全部 agent に任せること
- full command repair の一般解

## recovery の優先順位

### 1. executable / builtin / alias typo

候補源:

- builtin 名
- alias 名
- `/usr/bin/*`
- current dir の executable path

score 候補:

- 1-edit 距離
- prefix 一致
- transposition
- history 頻度

### 2. path recovery

対象:

- `cd`, `cat`, `ls`, `vi`, `grep` など path 引数を持つ command

既存 completion / glob helper を再利用し、
「最も近い path」を候補化する。

### 3. console error recovery

初期対象は限定する。

- `git push` upstream 未設定
- permission denied
- file not found
- directory not found

shell stdout/stderr の最後の数行を pattern match し、
定型 recovery 候補を返す。

### 4. LLM fallback

非 LLM recovery で候補が弱い時だけ、
agent へ「repair 提案」を依頼する。
この時点でも自動実行はしない。

## UI

### inline suggestion

prompt の下に 1 行だけ出す。

```text
suggest: ls
suggest: git push --set-upstream origin main
suggest: cd /home/user/docs
```

採用操作:

- `Tab`: 提案を line に反映
- `Enter`: suggestion 採用済みなら実行
- `Esc`: suggestion 破棄

### auto-apply ルール

MVP では次のみ自動反映候補にできる。

- 1-edit typo のみ
- non-destructive command
- 引数なしまたは path 誤りだけの correction

次は常に suggestion 止まり。

- `rm`
- `mv`
- `dd`
- `kill`
- `git push`
- network 書き込み系

## 実装方針

### 1. 既存 shell_completion の再利用

path 候補、履歴、lookup の一部はすでに shell 側にある。
新しい correction engine はそれらを呼び、
UI だけ `term` 側で扱う。

### 2. recovery と route を分ける

- Plan 01
  - shell / agent / recovery へ振り分ける
- Plan 02
  - recovery の中で何を出すか決める

この分離で router を単純に保つ。

### 3. error-aware recovery は bounded log で扱う

stderr 全文を毎回 agent に送らず、
末尾数行だけを recovery context に使う。
大きなログは artifact 扱いにする。

## 実装ステップ

1. correction engine の scoring API を作る
2. executable / alias / builtin typo を実装する
3. path recovery を追加する
4. console error pattern を追加する
5. destructive policy を入れる
6. host/QEMU test を追加する

## 変更対象

- 既存
  - `src/usr/command/term.c`
  - `src/usr/lib/libc/shell_completion.c`
  - `src/usr/lib/libc/shell_executor.c`
  - `src/usr/include/shell_completion.h`
- 新規候補
  - `src/usr/lib/libc/term_command_recovery.c`
  - `src/usr/include/term_command_recovery.h`
  - `tests/test_term_command_recovery.c`

## 検証

- host
  - `sl` -> `ls`
  - `pwc` -> `pwd`
  - `cd /hme/user` -> `cd /home/user`
  - `git push` error -> upstream 提案
  - `rm fiile` -> suggestion のみ、auto-apply しない
- QEMU
  - typo 候補の表示と採用
  - candidate reject 後の再入力

## 完了条件

- command-not-found に対して inline suggestion を出せる
- high-confidence typo を shell 非依存で修復候補化できる
- destructive command は suggestion 止まりになる
- host / QEMU で recovery path が固定される
