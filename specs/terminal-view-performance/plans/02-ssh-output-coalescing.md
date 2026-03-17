# Plan 02: SSH 出力 coalescing 強化

## 概要

`ssh_pump_tty_to_channel()` を 1 tick 1 回 read から、
byte budget 内でできるだけ PTY を drain する方式へ変える。

## 対象ファイル

- `src/net/ssh_server.c`
- `src/usr/lib/libc/vi_screen.c`
- `src/test/run_qemu_ssh_smoke.py`

## 実装要点

- stack 上の小さな一時バッファではなく global workspace を使う
- outbox / peer window / max packet を見ながら 1 tick で複数 packet を積む
- `CSI ? 2026 h/l` を frame marker として `SSH_METRIC` で観測する
- serial log 上で `pty_read_bytes` と `channel_data_avg_len` を確認できるようにする

## 状態

完了。
