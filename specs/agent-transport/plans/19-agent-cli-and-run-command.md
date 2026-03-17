# Plan 19: Agent CLI コマンドと run_command 強化

## 概要

Phase D-E で実装済みの `agent_run()` ループとツールシステムをユーザランドから
実際に使えるようにする。現在の `agent.c` はテストコマンドのまま、`run_command`
ツールはハードコード応答のみで、エージェントを実用的に使えない。

## ゴール

1. `agent "プロンプト"` でエージェントループが起動し、Claude API を使って自律的にタスクを完了する
2. `run_command` ツールが `execve` + `pipe` で実際のシェルコマンドを実行し、stdout を返す
3. エージェントが OS 内部の調査やシェル開発などの複雑なタスクを実行できる

## 前提

- `agent_run()`, `tool_init()`, `conversation.c`, `claude_client.c` は実装済み
- `pipe()` syscall、`execve()` syscall は動作済み
- `sh -c "command"` でコマンド実行が可能
- `start-stop-daemon.c` にパイプ経由 fd リダイレクトのパターンが存在
- API キーは `/etc/claude.conf` から読み込み（`ask.c` のパターンを踏襲）

## 制約

- fork() は未実装（`sys_fork()` は 0 を返すだけ）
- execve() は新しいプロセスを生成する（fork ではなく sibling プロセスを作る設計）
- パイプの stdout キャプチャは、execve 前に stdout を pipe の write 端に差し替え、
  親プロセスが read 端から読む方式で実現する

## 変更対象

### 1. `src/usr/command/agent.c` — 全面書き換え

テストコマンドから実際のエージェント CLI に書き換える。

```
Usage:
  agent <prompt>           — エージェントループを実行
  agent -s <max_steps>     — 最大ステップ数を指定（デフォルト: 10）
  agent -m <model>         — モデルを指定（デフォルト: agent.conf の値）
```

処理フロー:
1. `/etc/claude.conf` から API キーを読み込む
2. `/etc/agent/agent.conf` から設定を読み込む
3. `/etc/agent/system_prompt.txt` からシステムプロンプトを読み込む
4. entropy_init() + prng_init() で TLS 準備
5. tool_init() でツール登録
6. agent_config_init() → agent_load_config() → agent_run()
7. 結果を stdout に出力

### 2. `src/usr/lib/libagent/tool_run_command.c` — 強化

ハードコード応答から実際のコマンド実行に変更。

方式: `execve("/usr/bin/sh", ["sh", "-c", command], NULL)` で子プロセスを起動し、
stdout を pipe で親プロセスにリダイレクトして出力をキャプチャする。

処理フロー:
1. pipe(pipefd) で読み書きパイプを作成
2. stdout を pipefd[1] に差し替え（dup で保存）
3. execve で sh -c を起動
4. stdout を復元
5. pipefd[1] を close
6. pipefd[0] から出力を読み取り
7. waitpid で子プロセスの終了を待機
8. 出力と exit_code を JSON で返す

ビルトインコマンド（uname, whoami 等）はフォールバックとして残す。

### 3. `src/usr/lib/libagent/agent_loop.c` — provider 設定拡張

`agent_run()` で API キーを config から provider に渡す仕組みを追加。
現在は `config->provider` が NULL のとき `provider_claude` を使うが、
API キーのオーバーライドができない。

方式: `agent_config` に `api_key` フィールドは既にあるので、
`send_conversation()` 内で api_key があれば provider headers を動的に差し替える。

### 4. `src/usr/lib/libagent/claude_client.c` — 会話送信に API キー対応

`claude_send_conversation()` に API キーオーバーライドを追加。
既に `claude_send_message_with_key()` にパターンがあるので同様に。

## タスク

| ID | タスク | 完了条件 |
|---|---|---|
| AT-P19-01 | `agent.c` を CLI コマンドに書き換え | `agent "list files in /"` で agent_run() が呼ばれる |
| AT-P19-02 | `tool_run_command.c` に execve+pipe によるコマンド実行を実装 | `ls /usr/bin` の出力が tool_result に返る |
| AT-P19-03 | `claude_send_conversation_with_key()` を追加 | API キー付きで会話送信できる |
| AT-P19-04 | `agent_loop.c` の `send_conversation()` で API キー対応 | config.api_key が使われる |
| AT-P19-05 | ビルド確認と既存テストの regression チェック | `make` が通り、テストが壊れない |

## 完了条件

QEMU 上で以下が動作する:
```
$ agent "list the files in /usr/bin and tell me what commands are available"
```
→ Claude が list_dir や run_command を使い、ファイル一覧を取得して報告する
