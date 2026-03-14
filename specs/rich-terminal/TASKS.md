# Rich Terminal Tasks

`specs/rich-terminal/README.md` を、着手単位に落とした実装タスクリスト。
依存を崩さず順に進めることを前提にしている。

## 優先順

1. 互換を壊さない抽象化
2. graphics bring-up と文字セル描画
3. raw input と TTY/PTY
4. terminal client MVP
5. VT parser、resize、boot 統合
6. host/QEMU/perf の固定化
7. file/folder CRUD と基本コマンド
8. pipe / redirection と shell I/O 合成
9. フルスクリーン editor と保存導線
10. UTF-8 と多言語表示

## M0: 互換を壊さない基盤化

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-01 | `display_backend` と console context を定義し、`_kputc()` / `_kprintf()` の互換層を作る | なし | 既存 boot log が維持されたまま backend 差し替え点ができる |
| [x] | RT-02 | `vga.c` のカーソル、スクロール、色処理を VGA text backend へ分離する | RT-01 | `src/vga.c` から VRAM 直操作の責務が減り、backend 実装へ寄る |
| [x] | RT-03 | `SCREEN_WIDTH` / `SCREEN_HEIGHT` と `sys_core.S` の固定値依存を backend 参照へ置き換える | RT-02 | 80x25 が console API の表面から消える |
| [x] | RT-04 | `key_event` 構造体、scan code 変換、event queue を導入する | RT-01 | raw key event を保持でき、従来の文字列入力と共存できる |
| [x] | RT-05 | 旧 `set_stdin()` / `get_stdin()` を key event からの互換 adapter に落とす | RT-04 | `eshell` は未変更でも動き、terminal client 用 raw 経路が別で使える |

## M1: Graphics bring-up

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-06 | QEMU/Bochs 向け graphics device の検出と mode set を実装する | RT-03 | graphics mode に入れるか、失敗時に VGA text へ戻せる |
| [x] | RT-07 | `fb_info`, `putpixel`, `fillrect`, `blit`, `flush` を持つ framebuffer 層を作る | RT-06 | 単色塗りつぶしや矩形描画ができる |
| [x] | RT-08 | 8x16 系固定幅フォントと glyph 描画ルーチンを組み込む | RT-07 | 任意セル位置に ASCII 文字を描ける |
| [x] | RT-09 | `term_cell` / `terminal_surface` / dirty tracking を実装する | RT-08 | 可変列数の surface 更新とスクロールが pure logic で扱える |
| [x] | RT-10 | surface から framebuffer への cell renderer を作る | RT-09 | 80x25 を超える列数で文字列を描画できる |

## M2: Shell と terminal の分離

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-11 | `struct file` 周辺を見直し、stdio を特殊ケースではなく TTY 経路へ流せる形にする | RT-05 | `sys_read()` / `sys_write()` が TTY へ委譲できる |
| [x] | RT-12 | `tty` 本体、line discipline、canonical/raw の最小 subset を実装する | RT-11 | `ECHO` と行編集が TTY 側へ寄る |
| [x] | RT-13 | `pty master/slave` のペアとリングバッファを実装する | RT-12 | PTY 越しに双方向通信できる |
| [x] | RT-14 | shell 起動時に PTY slave を stdio へ割り当てる経路を作る | RT-13 | shell と console が分離される |
| [x] | RT-15 | raw input event を PTY へ流す最小 user/kernel API を定義する | RT-13 | terminal client が矢印キーや Ctrl を入力として送れる |

## M3: terminal client MVP

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-16 | `/usr/bin/term` の build、image 収録、起動骨格を作る | RT-10, RT-15 | `term` バイナリが build できる |
| [x] | RT-17 | `term` から framebuffer と input event を初期化し、列数・行数を計算する | RT-16 | terminal 側で viewport が確定する |
| [x] | RT-18 | `term` が PTY master を開き、子として `eshell` を起動する | RT-14, RT-16 | shell 出力を読み込める |
| [x] | RT-19 | `term` のメインループで PTY 読み取り、入力送信、damage redraw を回す | RT-17, RT-18 | shell 出力が framebuffer 上に見える |
| [x] | RT-20 | scrollback リングと全画面再描画経路を追加する | RT-19 | 長文出力後も履歴を保持できる |

## M4: terminal としての完成度を上げる

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-21 | VT parser 状態機械を実装し、通常文字と CSI を分離する | RT-19 | escape sequence を安全に解釈できる |
| [x] | RT-22 | `clear`、色、カーソル移動、消去、保存/復元の主要シーケンスを実装する | RT-21 | `clear` と `ls --color` が通る |
| [x] | RT-23 | 初期 winsize 伝播を `term` → shell に渡す | RT-18 | 120 列以上でも prompt が破綻しにくい |
| [x] | RT-24 | `src/usr/init.c` を `term` 起動中心へ切り替え、旧 `eshell` を fallback 化する | RT-20, RT-22, RT-23 | boot 後の既定 terminal が `term` になる |
| [x] | RT-25 | resize 通知と再描画を実装し、`eshell` の固定長前提を減らす | RT-23, RT-24 | 画面サイズ変更後も再計算と再描画ができる |

## M5: テストと性能固定化

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-26 | `tests/Makefile` に surface、VT、key、TTY の host test を追加する | RT-09, RT-12, RT-21 | pure logic の回帰テストが一括実行できる |
| [x] | RT-27 | VT fixture と期待 surface 比較のテストデータを用意する | RT-22, RT-26 | parser 退行を fixture で検知できる |
| [x] | RT-28 | graphics terminal 向け QEMU smoke test を追加する | RT-24 | boot から shell 表示まで自動確認できる |
| [x] | RT-29 | framebuffer dump または比較用スクリーンショット経路を用意する | RT-28 | 見た目差分を機械的に確認できる |
| [x] | RT-30 | 全面再描画、1 行スクロール、長文出力の計測ポイントを入れる | RT-20, RT-28 | 描画ボトルネックを数値で比較できる |

## M6: shell 上の file/folder CRUD を成立させる

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-31 | ext3 の namespace 更新 helper を整理し、`unlink` / `rmdir` / `rename` の土台を分離する | RT-26 | dentry 更新と bitmap 更新の責務が見通せる |
| [x] | RT-32 | `ext3_unlink`, `ext3_rmdir`, `ext3_rename` と対応 syscall を実装する | RT-31 | kernel から file/dir の削除と改名が呼べる |
| [x] | RT-33 | userland libc wrapper と `touch`, `mkdir`, `rm`, `rmdir`, `mv` を追加する | RT-32 | shell から基本コマンドで namespace を操作できる |
| [x] | RT-34 | 既存 `ls`, `cat`, `cd`, `pwd` と組み合わせた CRUD フローを確認する | RT-33 | create/read/update/delete の基本導線が破綻しない |
| [x] | RT-35 | file CRUD の host/QEMU smoke test を追加する | RT-32, RT-34 | 新規作成、改名、削除の回帰を検知できる |

## M7: shell の pipe / redirection を成立させる

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-36 | kernel に anonymous pipe と `pipe` / `dup` の最小 fd 制御を実装する | RT-35 | shell が pipe endpoint と fd 保存を扱える |
| [x] | RT-37 | child process へ stdio/file table を渡せる `execve` 経路を整える | RT-36 | `fork` 無しでも redirection 前提で command を起動できる |
| [x] | RT-38 | `eshell` の parser / executor を拡張し、`|`, `>`, `<` を扱う | RT-37 | `cmd > file`, `cmd < file`, `cmd1 | cmd2` が解釈できる |
| [x] | RT-39 | `cat` の stdin fallback と shell I/O 合成の基本コマンド検証を入れる | RT-38 | `cat < file` と `ls | cat` が成立する |
| [x] | RT-40 | pipe / redirection の host/QEMU smoke test を追加する | RT-38, RT-39 | shell I/O 合成の回帰を検知できる |

## M8: vi でファイル作成できる状態へ

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-41 | userland から TTY の `ICANON` / `ECHO` を切り替える最小 termios API を追加する | RT-24, RT-25 | `vi` が 1 キーずつ入力を受け取れる |
| [x] | RT-42 | `vi` 向けのキー解析と全画面再描画 helper を実装する | RT-41 | `ESC`, 矢印, `hjkl`, `:`, `Backspace`, `Enter` を扱える |
| [x] | RT-43 | `/usr/bin/vi` の editor buffer と `normal` / `insert` / `command-line` mode を実装する | RT-42, RT-40 | 新規空バッファを編集して画面に反映できる |
| [x] | RT-44 | `:w`, `:q`, `:wq` を実装し、Plan 10/11 の基盤上で保存する | RT-43, RT-40 | `vi memo.txt` から保存し、`cat memo.txt` で確認できる |
| [x] | RT-45 | `vi` の host/QEMU smoke test を追加する | RT-44, RT-28 | 新規ファイル作成フローを回帰検知できる |

## M9: UTF-8 と多言語表示

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-46 | UTF-8 decoder / encoder と不正シーケンス処理を pure logic として追加する | RT-27 | byte stream を Unicode scalar value へ安定変換できる |
| [x] | RT-47 | 表示幅計算と terminal surface の wide char 対応を実装する | RT-46 | 幅 0 / 1 / 2 の文字でカーソルと折り返しが破綻しない |
| [x] | RT-48 | `UDEV Gothic` から subset 済み bitmap を生成する `mkfontpack` と build 導線を追加する | RT-10, RT-47 | `font_default` 生成物を build 時に作れる |
| [x] | RT-49 | kernel に font registry と既定フォント読み込みを追加し、boot 時に `font_default` を読むようにする | RT-48 | early console と framebuffer backend が同じ既定フォントを使う |
| [x] | RT-50 | `term` 側の glyph lookup と fallback を kernel 既定フォント前提へ切り替える | RT-47, RT-49 | `term` が `UDEV Gothic` 由来半角 glyph と日本語 glyph を描画できる |
| [x] | RT-51 | `term`、`cat`、`vi` を UTF-8 / wide char 前提で再描画・編集できるようにする | RT-47, RT-50, RT-45 | 日本語を含む UTF-8 ファイルを表示・編集・保存できる |
| [x] | RT-52 | UTF-8 の host/QEMU smoke test を追加し、既定フォント描画と日本語ファイル導線を回帰検知できるようにする | RT-51, RT-29 | UTF-8 表示、既定フォント読み込み、保存の回帰を自動検知できる |
| [x] | RT-53 | `UDEV Gothic` の OFL 1.1 文書と attribution の同梱導線を追加する | RT-48 | ライセンス文書を欠かさず配布できる |

## 先送りする項目

- 複数 terminal セッション
- タブ補完や高度な line editing
- マウス入力
- ウィンドウシステム
- `vi` の undo/redo、検索、複数バッファ、visual mode
