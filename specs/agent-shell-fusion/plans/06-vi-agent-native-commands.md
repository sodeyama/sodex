# Plan 06: `vi` agent-native commands

## 概要

`vi` への統合は、
PTY key 注入ではなく editor-native command で進める。

本 Plan では、現在 buffer や visual selection を agent へ渡し、
質問、編集提案、review を `vi` から直接起動できるようにする。

## 初期 scope

- current buffer / visual selection を export する
- `:AgentAsk`
- `:AgentEdit`
- `:AgentFix`
- `:AgentReview`
- selection 単位の diff preview
- accept / reject
- undo/redo 整合

## 非ゴール

- multi-hunk partial apply の完全対応
- `vi` 全画面を PTY 注入で操縦すること
- insert mode 中の ghost text 補完

## command 契約

### `:AgentAsk <prompt>`

- current buffer または visual selection を文脈に質問
- 結果は scratch 表示または下部メッセージ領域へ出す

### `:AgentEdit <prompt>`

- visual selection があればその範囲を対象
- 無ければ current line または paragraph 相当を対象
- agent は replacement text を返す
- `vi` 側で diff preview を生成する

### `:AgentFix`

- `:AgentEdit` の shortcut
- obvious issue 修正を依頼する

### `:AgentReview`

- file 全体または selection を読み、
  finding のみを scratch 表示する

## diff preview

MVP は selection-scoped full replace に限定する。

表示方式:

- read-only scratch buffer
- または一時 preview pane

少なくとも次は見えるようにする。

- 対象範囲
- old text
- new text
- 行数差分

accept したら 1 回の編集操作として apply し、
undo 1 回で戻せるようにする。

## selection export

必要情報:

- file path
- buffer 全体または selected range
- range start/end
- cursor position
- filetype hint

agent へは全 buffer 全文ではなく、
必要範囲 + 周辺コンテキストを bounded で渡す。

## 実装方針

### 1. `vi` 内に小さな bridge を作る

`agent` CLI を PTY で起動し直すのではなく、
既存 libagent を直接呼べる bridge を `vi` 側へ足す。

### 2. apply は buffer API 経由で行う

文字列の生貼りではなく、
既存 `vi_buffer` の編集 API を使って変更する。
これにより undo/redo と整合させる。

### 3. review 出力と edit 出力を分ける

- review
  - text report
- edit
  - replacement candidate

返り値の型を分けることで UI が単純になる。

## 実装ステップ

1. selection export helper を作る
2. ex command を追加する
3. replacement candidate を受ける bridge を作る
4. diff preview と accept / reject を入れる
5. undo/redo 整合と test を追加する

## 変更対象

- 既存
  - `src/usr/command/vi.c`
  - `src/usr/lib/libc/vi_buffer.c`
  - `src/usr/lib/libc/vi_screen.c`
  - `src/usr/include/vi.h`
  - `src/usr/lib/libagent/repl.c`
- 新規候補
  - `src/usr/lib/libc/vi_agent.c`
  - `src/usr/include/vi_agent.h`
  - `tests/test_vi_agent.c`
  - `src/test/run_qemu_vi_agent_smoke.py`

## 検証

- host
  - selection export
  - range replace
  - preview accept / reject
  - undo/redo
- QEMU
  - `vi` で visual selection -> `:AgentEdit` -> preview -> accept
  - `:AgentReview` の finding 表示

## 完了条件

- `vi` から agent command を起動できる
- selection-aware な edit / review が成立する
- apply が undo/redo と整合する
- PTY 注入なしで `vi` 連携の主要経路が成立する
