# Plan 07: Regression Recovery と Signer-less 完全復旧

## 概要

userland `sshd` は一度 `test-qemu-ssh` が green になったが、
2026-03-16 の再調査時点では `bin/restart.sh server-headless --ssh`
単体で host から接続できない。

現在の主因は、`USERLAND_SSHD_BUILD` の KEX 経路だけが
`ssh_signer_port=0` でも external signer を必須扱いしていることにある。
この plan では、既定の signer-less 経路を復旧し、
最後に「host から普通に ssh できる」状態を受け入れ条件として固定する。

## 実施結果

- userland `sshd` の signer request 実装は `ssh_signer_roundtrip()` へ統一した
- `curve25519` shared secret の all-zero check を追加し、KEX close reason を `kexinit_invalid` / `kex_failed` / `newkeys_invalid` へ分けた
- `src/makefile` と `bin/restart.sh` の `ssh_signer_port` 既定値を `0` に揃えた
- `make -C src test-qemu-ssh` は signer-less 既定で green
- `bin/restart.sh server-headless --ssh` の後、README 記載の host 側 `ssh` 手順で login / `exit` を確認した

## 現状の問題整理

- `src/net/ssh_server.c` の userland KEX は `ssh_request_host_curve25519()` と `ssh_request_host_signature()` を無条件で使う
- その userland 実装は `ssh_signer_port<=0` だと即失敗する
- 一方で kernel には `SYS_CALL_SSH_SIGNER_ROUNDTRIP` があり、`port<=0` なら local crypto、`port>0` なら host signer に送る helper が既にある
- さらに protocol gap として、`curve25519` shared secret の all-zero check が無く、KEX failure が `protocol_error` に潰れている

## 方針

1. まずは signer request 経路を 1 つに統一し、signer-less 既定経路を最短で復旧する
2. 次に KEX 実装を protocol 的に妥当な形へ寄せる
3. その後に起動 script、smoke、README を現在の実装と一致させる
4. 最後に manual / smoke の両方で受け入れ条件を再固定する

## 実装フェーズ

### Phase 1: 既定経路の復旧

- userland `sshd` の signer request 実装を `ssh_signer_roundtrip()` ベースへ寄せる
- `ssh_signer_port=0` では local fallback を使う
- `ssh_signer_port>0` のときだけ host signer を使う
- `bin/restart.sh server-headless --ssh` 単体で password prompt まで進むことを最優先にする

## Phase 2: KEX 実装の整理

- `curve25519` shared secret が all-zero のとき KEX failure にする
- signer を使う場合と使わない場合で、KEX hash / sign / `NEWKEYS` の流れが同じになるように揃える
- 可能なら `curve25519` 計算は guest 側に固定し、signer は host key 署名の委譲に寄せる

## Phase 3: failure semantics と観測性

- `protocol_error` 一括 close をやめ、`kex_failed_*` のような audit を足す
- 必要なら `SSH_MSG_DISCONNECT` を返し、`KEY_EXCHANGE_FAILED` と `PROTOCOL_ERROR` を分ける
- serial / smoke だけで失敗位置が分かるようにする

## Phase 4: 起動導線とテストの整合

- signer-less を既定とする
- signer mode を残すなら opt-in として明示する
- `start.sh` / `restart.sh` / smoke / README / spec の期待値を一致させる

## 具体タスク

1. `USERLAND_SSHD_BUILD` の signer request 実装を `ssh_signer_roundtrip()` に統一する
2. signer-less 既定で KEX -> `NEWKEYS` -> `password` auth まで通す
3. `curve25519` all-zero check を入れる
4. KEX failure 時の audit / close reason を分ける
5. signer mode を opt-in として残すか削るかを決める
6. `test-qemu-ssh` を signer-less 既定で green に戻す
7. 手動手順を `bin/restart.sh server-headless --ssh` と README 記載の host 側 `ssh` 手順に固定する

## 検証順

1. host unit 相当で `ssh_signer_roundtrip(port=0)` の local fallback を確認する
2. QEMU で `listener_ready kind=ssh` から `ssh_newkeys_rx` まで確認する
3. OpenSSH client で login / wrong password / reconnect を確認する
4. signer mode を残す場合だけ、host signer 付きでも同じ smoke を回す

## 完了条件

- `bin/restart.sh server-headless --ssh` 単体で userland `sshd` が動く
- host から README 記載の `ssh` 手順で login / `exit` できる
- `test-qemu-ssh` が signer-less 既定で green
- `wrong password` と reconnect が通る
- spec / script / smoke / README の期待値が一致している
