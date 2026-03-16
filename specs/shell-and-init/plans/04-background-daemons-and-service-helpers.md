# Plan 04: Background Daemon と Service Helper

## 概要

`/etc/init.d/sshd start` のような script を成立させるため、
background 実行、daemon detach、pidfile/status を整える。

## MVP

- `cmd &`
- shell が `$!` と known pid を保持する
- `wait [pid...]`
- service helper による
  - start
  - stop
  - restart
  - status
- pidfile
- stdio redirect
- log file 出力

## shell `&` だけでは足りない理由

raw shell だけで service を書くと、
次の責務が毎回 script に散る。

- pidfile の作成/削除
- stale pidfile 判定
- `kill -TERM` と timeout
- `status` の exit code
- stdin/stdout/stderr の扱い
- detach / new session

そのため script には shell を使うが、
service 運用の共通部分は helper に寄せる。

## helper の候補

1. `/usr/bin/service`
   - `service sshd start` の entrypoint
2. `/usr/bin/start-stop-daemon` 互換の最小版
   - 実体はこちらでもよい
3. `/etc/init.d/rc.common`
   - shell function library

MVP では 2 と 3 の併用が実装しやすい。

## detach 方針

- 可能なら `setsid()` または `POSIX_SPAWN_SETSID` 風 flag を使う
- 最低でも TTY foreground から切り離し、
  stdin/stdout/stderr を明示的に処理する
- `/dev/null` がない場合でも、helper 側で安全な代替を持つ

## status 契約

LSB 風に、少なくとも次を揃える。

- `0`: running / OK
- `3`: not running
- `4`: status unknown

pidfile を採るなら stale pidfile の扱いも決める。

## 実装メモ

2026-03-17 の hardening で、service helper の failure-path まで固定した。

- `start-stop-daemon` は stdin を EOF pipe に差し替え、
  TTY foreground pid を外して daemon の入力依存を切る
- `stdout` / `stderr` redirect に加えて、
  `SIGTERM` -> timeout -> `SIGKILL` fallback を実装した
- `force-reload` を追加し、
  running / not-running / invalid pidfile の exit status を整理した
- `--require` で binary / config 前提を共通チェックし、
  stale pidfile は `status=3` 側で掃除する
- `run_qemu_service_contract_smoke.py` で
  `stop`, `force-reload`, stale pidfile, prerequisite failure を固定した

`sodex` にはまだ `setsid()` 相当の session API は無いので、
この phase では TTY foreground 切り離しと stdio redirect を
detach 契約として採用する。

## 変更対象

- 既存
  - `src/process.c`
  - `src/signal.c`
  - `src/usr/command/kill.c`
  - `src/usr/command/ps.c`
- 新規候補
  - `src/usr/command/service.c`
  - `src/usr/command/start-stop-daemon.c`
  - `src/usr/lib/libc/service_helper.c`
  - `src/usr/include/service_helper.h`

## 検証

- `sh -c '/usr/bin/sshd & echo $!'` で PID が取れる
- helper が pidfile を作り、stop/status を返せる
- 同名 service の二重起動を sensibly に扱える

## 完了条件

- service script が raw shell だけで無理をしなくて済む
- `sshd` を background service として起動/停止/確認できる
- daemon の stdio と pid 管理方針が固定される
