# Plan 02: SSH 出力 coalescing 強化

## 概要

`ssh_pump_tty_to_channel()` の出力粒度を local `term` 並みに引き上げ、
vi の full redraw（改善前）や差分更新（改善後）が
少ない packet 数・少ない tick 数で client に届くようにする。

## 対象ファイル

- `src/net/ssh_server.c` — `ssh_pump_tty_to_channel()` 本体
- `src/usr/lib/libc/vi_screen.c` — synchronized output の挿入点
- `src/test/run_qemu_ssh_smoke.py` — 改善確認

## 現状の問題

- `raw_chunk[128]` で 1 tick あたり最大 128 byte しか吸い上げない
- `cooked_chunk[256]` の CRLF 変換バッファも小さい
- 1 tick = 10ms（HZ=100）で loop しているため、2KiB の redraw が 16 tick に分割される
- local `term` は 8KiB まで drain してから描画するのに対し、SSH は桁違いに細かい
- ただし drain を無制限にすると input や keepalive を圧迫するので、per-tick 予算は必要

## 設計

### read cap 引き上げ（TVP-06）

`raw_chunk` と `cooked_chunk` の上限を引き上げるが、
stack 常駐固定長だけに頼らない。
peer window と max packet size を見ながら、
1 tick あたりの byte budget も合わせて決める。

### drain loop 化（TVP-07）

現状の「1 tick で 1 回 read」を
「PTY が空になるか outbox/window/byte budget が尽きるまで loop」に変更する。

```
while (pty has data &&
       ssh window > 0 &&
       outbox has room &&
       tick_budget > 0) {
    read from pty (up to min(buf_size, ssh_window))
    crlf convert
    build SSH packet
    enqueue to outbox
}
```

### CRLF バッファ動的化（TVP-08）

固定 256 byte をやめ、read した raw 量の 2 倍
（最悪ケース: 全 LF → CRLF）を扱える workspace に変える。
stack を大きくしすぎない実装を優先する。

### packet まとめ（TVP-09）

peer の max packet size、window、outbox slot を尊重しつつ、
1 回の drain で読んだデータをなるべく少数 packet にまとめて送信する。

### synchronized output（TVP-10）

vi 側で redraw 出力の前後に `CSI ? 2026 h` / `CSI ? 2026 l` を挿入する案は維持するが、
無条件導入にはしない。

- 非対応 terminal を壊さないこと
- local `term` に不要な依存を作らないこと
- SSH メトリクスで redraw 境界を観測できること

を満たす gated な導入にする。

## 実装ステップ

1. TVP-06: read cap / tick budget 見直し
2. TVP-07: drain loop 化
3. TVP-08: CRLF workspace 拡張
4. TVP-09: packet まとめ
5. TVP-10: gated な frame marker 対応

## リスク

- drain loop が 1 tick を使い切って入力処理が遅延する
  - 対策: drain 量に上限（例: 16KiB/tick）を設ける
- 大きな packet が peer の受信バッファを溢れさせる
  - 対策: SSH window size を必ず尊重する
- redraw 境界が見えないままでは `packets_per_redraw` を正しく計測できない
  - 対策: TVP-10 の frame marker とセットで導入する
