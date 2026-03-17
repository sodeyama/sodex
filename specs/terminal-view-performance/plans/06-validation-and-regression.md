# Plan 06: 検証と回帰固定化

## 概要

差分描画、scroll fast path、SSH coalescing を host test と QEMU smoke の両方で固定する。

## 対象ファイル

- `tests/test_vi_screen.c`
- `tests/test_terminal_surface.c`
- `tests/test_lib.c`
- `src/test/run_qemu_terminal_smoke.py`
- `src/test/run_qemu_vi_smoke.py`
- `src/test/run_qemu_ssh_smoke.py`

## 実装要点

- host 側で差分描画と scroll dirty 管理を固定する
- QEMU term smoke で `present_copy_area` と `scroll_fast_path` を見る
- QEMU vi smoke で `TERM_METRIC component=vi` を見る
- QEMU ssh smoke で `SSH_METRIC` の数値フィールドを確認する

## 状態

完了。
