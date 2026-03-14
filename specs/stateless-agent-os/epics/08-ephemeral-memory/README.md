# EPIC 08: Ephemeral MemoryとCrypto Erasure

## 目的

一時データと資格情報をメモリ上だけで扱い、タスク完了時に安全に消す。
「全部を最初から暗号化」ではなく、秘密情報と高リスクバッファから段階導入する。

## スコープ

- セキュアゼロ化
- タスク固有鍵
- 暗号化バッファ
- クリーンアップフック
- メモリ使用量の上限管理

## 実装ステップ

1. `memset` 最適化で消去が消されない `secure_memzero()` を用意する
2. タスク単位の鍵生成と破棄 API を定義する
3. シークレット、HTTPボディ、MCP結果など対象バッファの優先順位を決める
4. 暗号化バッファ allocator を作り、通常 allocator と区別する
5. タスク完了、失敗、タイムアウトの全経路で cleanup を走らせる
6. i486 前提でオーバーヘッドを測り、暗号化対象を段階的に広げる

## 変更対象

- 新規候補
  - `src/security/secure_memory.c`
  - `src/include/security/secure_memory.h`
  - `src/security/task_keys.c`
  - `src/include/security/task_keys.h`
  - `src/security/crypto_buffer.c`
  - `src/include/security/crypto_buffer.h`
  - `tests/test_secure_memory.c`
- 既存
  - `src/memory.c`
  - `src/agent/agent_loop.c`
  - `src/security/secret_store.c`

## テスト

- host 単体
  - secure zeroization
  - key lifecycle
  - allocator の境界条件
- 結合
  - タスク成功/失敗時 cleanup
  - timeout cleanup

## 完了条件

- シークレットと高リスク一時データを優先的に消去できる
- cleanup の抜け道が主要経路に残っていない
- 暗号化対象のコストを測定し、段階導入方針を文書化できている
- 認証情報と Agent Loop が同じ cleanup モデルを使う

## 依存と後続

- 依存: EPIC-06, EPIC-07
- 後続: EPIC-10

