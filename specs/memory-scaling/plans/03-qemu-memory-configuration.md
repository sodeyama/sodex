# Plan 03: QEMU memory 設定の一元化

## 概要

今は `bin/start.sh` と各種 QEMU smoke script がそれぞれ `-m 128` を直書きしており、
高メモリ構成を試すたびに複数箇所を直す必要がある。

この plan では build 時の既定値と runtime override を整理し、
QEMU RAM と guest memory cap を一貫した knob で制御できるようにする。

## 依存と出口

- 依存: 01
- この plan の出口
  - `start.sh` と QEMU smoke test が同じ memory setting を使う
  - build 既定値、runtime override、guest cap の優先順位が決まる
  - 開発者が `128MB` と `1GB` を簡単に切り替えられる

## 現状

- `bin/start.sh` が `-m 128` を直書きしている
- `run_qemu_ktest.py` など各 smoke script も `-m 128` を個別に持つ
- RAM 量を変える単一の設定名や helper が無い

## 方針

- QEMU の RAM 量は runtime override を第一にする
- build 時の既定値は repo 共通の変数で持つ
- guest が使う RAM cap は QEMU `-m` と別に切れるようにする

## 設計判断

- 例として以下の 2 系統を持つ
  - `SODEX_QEMU_MEM_MB`
  - `SODEX_RAM_CAP_MB`
- `SODEX_QEMU_MEM_MB` は `bin/start.sh` と Python smoke scripts の両方で解釈する
- `SODEX_RAM_CAP_MB` は kernel build-time define または boot arg で guest 側上限に使う
- QEMU 引数の組み立ては helper 化し、script ごとのコピペを減らす

## 実装ステップ

1. 共通の memory setting 名と優先順位を決める
2. `bin/start.sh` をその設定で起動するよう変える
3. 各 QEMU smoke script の `-m` 直書きを helper へ寄せる
4. make target から memory setting を渡しやすくする
5. README と spec に利用方法を追記する

## 変更対象

- 既存
  - `bin/start.sh`
  - `src/makefile`
  - `src/test/run_qemu_ktest.py`
  - `src/test/run_qemu_terminal_smoke.py`
  - `src/test/run_qemu_fs_crud_smoke.py`
  - `src/test/run_qemu_shell_io_smoke.py`
  - `src/test/run_qemu_vi_smoke.py`
  - `src/test/run_qemu_utf8_smoke.py`
  - `src/test/run_qemu_ime_smoke.py`
- 新規候補
  - `src/test/qemu_config.py`

## 検証

- `bin/start.sh` で既定値起動と `1GB` override の両方ができる
- 各 smoke script が同じ override を解釈する
- memory cap を下げた guest でも QEMU RAM 大きめ構成を安全に試せる

## 完了条件

- QEMU RAM 量を変える操作が 1 箇所の設定で済む
- memory scaling の検証と日常開発の往復が軽くなる
