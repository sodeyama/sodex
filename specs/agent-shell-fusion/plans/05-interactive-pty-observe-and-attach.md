# Plan 05: interactive PTY observe と attach

## 概要

次の段階は、agent が running PTY を読めることだ。
ただし、いきなり自由入力を許すと壊れやすい。

そのため本 Plan では、
まず observe-only attach を作り、
PTY write は approval 付きに限定する。

## 初期 scope

- foreground PTY の viewport / recent output を agent が読める
- app 種別を大まかに判定する
- agent session 用の簡易 prompt 契約を入れる
- PTY write は confirmation 付きで導入する

## 非ゴール

- full autonomous `vim` 操作
- 任意 app への長時間 script 注入
- terminal emulator 全状態の完全同期

## observe の単位

agent へ渡す情報は次に限定する。

- 現在画面の visible rows
- recent scrollback tail
- cursor position
- foreground command 名
- mode hint
  - shell-like
  - pager-like
  - repl-like
  - full-screen

画面全履歴や raw VT stream 全文は渡さない。

## app classification

初期対象:

- `less` / `more`
- `python` / `sxi`
- `psql` / `mysql` 相当
- `gdb`
- `vi`

判定材料:

- foreground argv
- termios / alternate screen
- prompt pattern

## prompt simplification

Cursor の知見と同様に、
agent attach 中は heavy prompt が邪魔になる。

そのため child shell へ環境変数を渡す。

例:

```text
SODEX_AGENT_SESSION=1
```

これが立っているときは shell prompt を簡易表示へ落とせるようにする。
MVP では prompt 内容の簡略化だけでよい。

## PTY write

write は observe より一段強い権限として扱う。

MVP の制約:

- 1 回の input block ごとに approval
- paste 量に上限
- `vi` では既定 deny

`vi` は後続 Plan 06 で native integration を作るので、
PTY 注入を正道にしない。

## 実装方針

### 1. VT parser / surface を再利用する

`term` がすでに持っている表示内容を、
raw PTY バイト列から再構築し直さない。
画面表現は terminal surface から抜く。

### 2. observe path と write path を分ける

同じ attach でも権限が違う。

- observe
  - read-only
- write
  - confirmation 必須

### 3. `vi` は特別扱いする

`vi` を PTY app の 1 つとして分類はするが、
write は既定 deny にして native path へ寄せる。

## 実装ステップ

1. PTY observe API を定義する
2. terminal surface から visible rows を抽出する
3. foreground app classification を追加する
4. attach UI を drawer に足す
5. PTY write approval を追加する

## 変更対象

- 既存
  - `src/usr/command/term.c`
  - `src/usr/include/terminal_surface.h`
  - `src/usr/lib/libc/vi_screen.c`
- 新規候補
  - `src/usr/lib/libagent/pty_observer.c`
  - `src/usr/include/agent/pty_observer.h`
  - `tests/test_pty_observer.c`
  - `src/test/run_qemu_agent_pty_attach_smoke.py`

## 検証

- host
  - visible rows export
  - cursor position export
  - app classification
  - write approval deny
- QEMU
  - `less` / `python` / `sxi` へ observe attach
  - prompt 簡略化環境変数の確認
  - PTY write の許可 / 拒否

## 完了条件

- agent が running PTY の visible 状態を読める
- heavy prompt を避ける child shell 契約がある
- PTY write は approval 付きでのみ実行される
- `vi` への直接 PTY write は既定 deny になる
