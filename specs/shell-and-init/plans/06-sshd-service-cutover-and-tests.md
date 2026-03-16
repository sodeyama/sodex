# Plan 06: `sshd` Service Cutover と Test

## 概要

`/usr/bin/sshd` の protocol 実装は既存 spec に委ねつつ、
起動導線だけを `service script` 化する。

この phase の主題は、
`init.c` 直書きの `execve("/usr/bin/sshd")` をやめて、
`/etc/init.d/sshd start` で boot 起動すること。

## やること

1. `/etc/init.d/sshd` を追加する
2. `start/stop/restart/status` の action を実装する
3. pidfile / log / exit code を helper 経由で固定する
4. `rcS` から `sshd start` を呼ぶ
5. `src/usr/init.c` の hardcode 起動を削る

## `sshd` script の責務

- binary の存在確認
- config の前提確認
  - `/etc/sodex-admin.conf`
  - `ssh_*` 設定
- 二重起動防止
- stop 時の signal 送出
- status の exit code

## 実装メモ

2026-03-17 の hardening で、`sshd` service の契約と failure-path を固定した。

- `/etc/init.d/sshd` は `--require /usr/bin/sshd` と
  `--require /etc/sodex-admin.conf` を通して前提を明示確認する
- `status`, `restart` に加えて `stop`, `force-reload`,
  invalid pidfile, prerequisite failure の結果を smoke で固定した
- `run_qemu_service_smoke.py` で再起動後の再接続を確認し、
  `run_qemu_service_contract_smoke.py` で exit status 契約を固定した

## `term` との関係

初回 cutover では `sshd` だけを service script 化してよい。
`term` は mode によっては foreground interactive program なので、
daemon と同列に扱わず別判断でもよい。

ただし最終的には、
`init` がどの mode で何を foreground に持つかを
この spec 側で明文化する。

## 検証

- boot だけで `sshd` が自動起動する
- host から SSH 接続できる
- `service sshd status` が妥当な exit status を返す
- `service sshd restart` 後も再接続できる
- `ps` に `sshd` が見える

## 変更対象

- 既存
  - `src/usr/init.c`
  - `src/test/run_qemu_ssh_smoke.py`
  - `src/test/write_server_runtime_overlay.py`
- 新規候補
  - `/etc/init.d/sshd`
  - `src/test/run_qemu_init_rc_smoke.py`
  - `src/test/run_qemu_service_smoke.py`

## 完了条件

- `sshd` が `init.c` hardcode ではなく service script で起動する
- SSH smoke が新しい boot 導線で green になる
- service action の manual 手順と smoke が一致する
