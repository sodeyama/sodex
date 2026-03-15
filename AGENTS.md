# AGENTS.md

## 言語
- 回答は日本語で簡潔に行う。
- 新しく書くコードコメントは日本語にする。
- 既存の英語コメントやファイルヘッダは、必要がなければ訳さない。

## プロジェクト概要
- `sodex` は i486 向けの自作 OS を QEMU 上で開発・検証している repo。
- 生成対象の大半は freestanding な 32bit コード。`src/` と `src/usr/` では外部ライブラリや host libc 前提を持ち込まない。
- host 側で通常の runtime を使ってよいのは主に `tests/`、`src/tools/`、`src/test/*.py`。

## 最初に読む場所
- まず `README.md` で現状の到達点と主要コマンドを把握する。
- terminal / shell / `vi` / UTF-8 / IME は `specs/rich-terminal/README.md` と `specs/rich-terminal/TASKS.md` を読む。
- memory / allocator / QEMU RAM は `specs/memory-scaling/README.md` と `specs/memory-scaling/TASKS.md` を読む。
- network は `specs/network-driver/README.md`、server 系は `specs/server-runtime/README.md`、テスト整備は `specs/testing-and-refactoring/README.md` を読む。
- `CLAUDE.md` は補助資料として参照してよいが、食い違いがあれば `README.md`、`specs/`、`makefile*` を優先する。

## ディレクトリの見方
- `src/`: カーネル、ブート、ドライバ、TTY、display、network、本体テスト。
- `src/usr/command/`: userland コマンドのソース。
- `src/usr/lib/` と `src/usr/include/`: userland libc と syscall wrapper。
- `src/usr/bin/`: 生成済み ELF。編集元ではない。
- `src/tools/`: フォント生成、IME 辞書生成、イメージ生成などの host ツール。
- `src/test/`: QEMU smoke / integration script と参照データ。
- `tests/`: host unit test と mock。`test_*` バイナリや `.o` は生成物。
- `build/`: カーネル build 成果物、QEMU ログ、map、image の出力先。
- `third_party/`: `UDEV Gothic` と Mozc 辞書入力。
- `specs/`: 設計書と task 管理。

## 作業ルール
- 既存の局所スタイルに合わせる。target C では 2 space indent と `PUBLIC` / `PRIVATE` / `EXTERN` マクロを使う箇所が多い。
- guest 向けコードに `printf`、host の `malloc` / `free`、標準 libc 依存、外部ライブラリ依存を持ち込まない。必要なら既存実装を使うか、この repo 内で最小実装を足す。
- 新しいロジックは、可能なら hardware 依存部と pure logic を分け、`tests/` で host unit test できる形を優先する。
- QEMU 固有の確認や回帰は `src/test/` の smoke script に寄せる。表示系の意図的変更では `src/test/data/*.json` の参照更新も検討する。
- 生成物を手で編集しない。特に `build/**`、`tests/test_*`、`tests/*.o`、`src/usr/bin/*`、`src/usr/lib/libc/libc.a`、`src/include/font8x16_data.h`、`src/include/font16x16_data.h` は編集元ではない。
- フォントや IME 辞書を変えるときは、`src/tools/`、`src/tools/ime_dictionary_manual.tsv`、`third_party/fonts/`、`third_party/dictionaries/mozc/` を編集して再生成する。
- QEMU の serial/debug/monitor の出力は `build/log/` に集約する。`src/` や `tests/` にログを散らさない。
- 実装の完了条件や scope が `specs/` で管理されている領域を触ったら、必要に応じて対応する `TASKS.md` や plan も更新する。

## ビルドと検証
- ルート `makefile` は `src/` と `tests/` への委譲だけなので、必要に応じて `make -C src` / `make -C tests` を直接使ってよい。
- 基本 build: `make`
- host unit test: `make test`
- kernel integration: `make test-qemu`
- 主な smoke:
  - `make -C src test-qemu-memory`
  - `make -C src test-qemu-user-memory`
  - `make -C src test-qemu-term`
  - `make -C src test-qemu-fs`
  - `make -C src test-qemu-shell-io`
  - `make -C src test-qemu-vi`
  - `make -C src test-qemu-ime`
- 通常起動: `bin/start.sh`。ネットワーク付きは `bin/start.sh net`。
- QEMU RAM の制御は `SODEX_QEMU_MEM_MB`、guest 側 cap は `SODEX_RAM_CAP_MB`。QEMU 起動経路を触るときはこの命名を維持する。
- 検証は変更箇所に最も近い最小セットから始め、表示・IME・QEMU 起動経路を触ったときだけ該当 smoke を広げる。
