# Plan 16: terminal / shell / vi の実用化と堅牢化

## 概要

Plan 15 までで、graphics terminal、shell、`vi`、UTF-8、日本語直接入力の主要導線は一通り成立した。
ただし日常的に使うにはまだ荒い部分が残っている。

- `make -C tests test` の rich terminal 系集約テストが壊れている
- `term` は無入力時に busy loop で待機している
- graphics mode 側の resize 伝播が弱い
- `eshell` は空白 split と 1 本の pipe / 単一 redirection に強く依存している
- `vi` は alternate screen、undo/redo、検索、visual mode が未対応で、全画面再描画に寄っている

この plan では、terminal / shell / `vi` を「動く」状態から「継続的に使える」状態へ引き上げる。
最初のゴールは回帰基盤を再び green に戻し、その上で idle 時の挙動、shell parser、`vi` の実用操作を整えること。

## 依存と出口

- 依存: 09, 11, 13, 15
- この plan の出口
  - `make -C tests test` が rich terminal 系を含めて再び通る
  - `term` が idle 時に busy wait へ依存せず、graphics mode でも winsize 更新を伝播できる
  - `eshell` が quote-aware tokenization、複数段 pipeline、`>>` を扱える
  - `vi` が alternate screen で出入りし、終了時に shell 画面を自然に復元できる
  - `vi` が `u` / `Ctrl-R`、`/` / `?` / `n` / `N`、最小 visual mode を扱える
  - host test と QEMU smoke で上記の回帰を検知できる

## 現状

- `tests/Makefile` の集約 runner は依存抜けで壊れやすく、rich terminal 系を含む一括実行を継続的に守る必要がある
- `term` は PTY と input event を回し続け、無入力時も spin で待機している
- `term` の viewport 同期は console path に寄っており、framebuffer path は再計算余地が残っている
- `eshell` は token を単純に空白で切っており、quoted string や複数段 pipeline を扱えない
- `vi` は通常画面を全消去して描き直す前提で、終了後の復元や編集履歴の概念がない

## 方針

- まず回帰基盤を直し、集約 test を常時使える状態に戻す
- terminal の待機は単一スレッドのまま改善する
  - 大掛かりな scheduler や signal 導入はしない
- shell parser は最小 POSIX subset を目指す
  - quoting
  - backslash escape
  - 複数段 pipeline
  - `>>`
- `vi` は「日常編集に頻出する機能」を優先する
  - alternate screen
  - undo/redo
  - 検索
  - visual mode
- 日本語 IME の辞書変換と候補 UI は Plan 17 へ分離する

## 設計判断

- 集約 host test を release gate として扱う
  - 単体 test が増えても `make -C tests test` 1 本で壊れ方が見える状態を維持する
- `term` の idle 改善は、まず「無入力で空回りしないこと」を優先する
  - 完全な event multiplexer よりも、既存 syscall と相性の良い最小 wait を優先する
- resize はまず winsize 再通知と再描画を優先する
  - scrollback の再折り返しや高度な reflow は後段に回す
- shell は tokenizer と executor を分離する
  - 空白 split から抜け、将来の構文拡張を局所化する
- `vi` の alternate screen は VT sequence を優先し、非対応時だけ全画面再描画へ戻す
- undo/redo は最初は最小履歴でよい
  - 高度な branching history は持たない
- 検索はまず plain text の UTF-8 部分一致でよい
  - regex や置換は非ゴールにする
- visual mode は char-wise と line-wise の最小 subset から始める
  - blockwise は後回しにする

## 実装ステップ

1. `tests/Makefile` の rich terminal 系 test 依存を修正し、集約 host test を再び green に戻す
2. `term` の idle loop を見直し、無入力時の spin を減らす
3. framebuffer path でも viewport 変化を検出し、winsize 再通知と再描画を通す
4. `eshell` に quote-aware tokenizer を追加し、空白 split 依存を解消する
5. `eshell` の parser / executor を拡張し、複数段 pipeline と `>>` を扱えるようにする
6. shell parser / I/O 合成の host/QEMU test を拡張する
7. `vi` に alternate screen の入退場を追加し、終了時の画面復元を整える
8. `vi` に undo/redo、検索、visual mode の最小 subset を追加する
9. `vi` の host/QEMU test を拡張し、日常編集フローを固定する

## 変更対象

- 既存
  - `tests/Makefile`
  - `src/usr/command/term.c`
  - `src/usr/command/eshell.c`
  - `src/usr/command/vi.c`
  - `src/usr/lib/libc/vi_screen.c`
  - `src/usr/lib/libc/vi_buffer.c`
  - `src/usr/include/vi.h`
  - `src/usr/include/winsize.h`
  - `src/tty/tty.c`
  - `src/test/run_qemu_shell_io_smoke.py`
  - `src/test/run_qemu_vi_smoke.py`
- 新規候補
  - `tests/test_eshell_parser.c`
  - `tests/test_term_loop.c`
  - `src/test/run_qemu_shell_advanced_smoke.py`

## 検証

- `make -C tests test` が rich terminal 系を含めて通る
- 無入力の `term` が従来より低負荷で待機する
- graphics terminal で viewport 変化後も prompt と `vi` 画面が破綻しない
- `echo "a b"` のような quoted string を `eshell` が壊さない
- `cmd1 | cmd2 | cmd3` と `cmd >> file` が成立する
- `vi` が起動時に alternate screen へ入り、終了後に shell 表示へ戻る
- `u` / `Ctrl-R`、`/needle`、`?needle`、`n` / `N`、visual mode の最小操作が通る
- QEMU 上でも shell 復帰と保存導線が継続して動く

## 完了条件

- terminal / shell / `vi` が日常利用で目立つ粗さを大きく減らす
- rich terminal 系の回帰が host / QEMU で継続的に見える
- Plan 17 へ進む前提として、入力・画面・parser の基盤が安定する
