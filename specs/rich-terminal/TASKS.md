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
11. 日本語直接入力と IME
12. terminal / shell / `vi` の実用化と回帰基盤の再固定
13. 日本語 IME の漢字変換と候補 UI
14. フル IME 辞書と大規模候補対応

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

## M10: 日本語直接入力

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-54 | `term` の入力変換を raw key と text commit に分離し、IME を差し込める形へ整理する | RT-15, RT-51 | 矢印、Ctrl、`Esc` と printable key の扱いを分けられる |
| [x] | RT-55 | `term` に `latin` / `hiragana` / `katakana` の mode 状態と切り替えキーを追加する | RT-54 | US 配列でも IME を ON/OFF でき、将来の `半角/全角` 系キーを同じ操作へ束ねられる |
| [x] | RT-56 | romaji からかなへの変換器と preedit buffer を pure logic として実装する | RT-55, RT-46 | 未確定入力、確定、Backspace が UTF-8 を壊さず動く |
| [x] | RT-57 | `term` に IME overlay を追加し、確定 UTF-8 を PTY へ流す経路を shell / `vi` で通す | RT-56, RT-51 | 日本語直接入力が `vi` と shell に届き、状態表示も見える |
| [x] | RT-58 | TTY canonical 編集と echo を UTF-8 文字境界対応にする | RT-57, RT-46 | multibyte 入力後の Backspace が 1 文字単位で動く |
| [x] | RT-59 | IME の host/QEMU test を追加し、切り替え、日本語入力、保存を回帰検知できるようにする | RT-57, RT-58, RT-52 | 日本語直接入力の主要導線を自動検知できる |

## M11: vi 基本編集の実用化

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-60 | `vi_buffer` に行頭末尾移動、単語移動、範囲削除、行削除の helper を追加する | RT-51 | UTF-8 文字境界を壊さずに motion と delete 範囲を計算できる |
| [x] | RT-61 | `vi` の normal mode に operator-pending と複合キー parser を追加する | RT-42, RT-60 | `d` と `g` 系の複合入力を解釈できる |
| [x] | RT-62 | `0`, `^`, `$`, `w`, `b`, `e`, `gg`, `G`, `x`, `X`, `dd`, `D`, `dw`, `db`, `de`, `d0`, `d$`, `a`, `A`, `I`, `o`, `O` を実装する | RT-61 | 既存ファイルに対する基本編集が normal mode だけで成立する |
| [x] | RT-63 | `vi` の host test を拡張し、ASCII / UTF-8 の移動と削除を回帰検知できるようにする | RT-60, RT-62 | pure logic の編集退行を自動検知できる |
| [x] | RT-64 | `vi` の QEMU smoke test を拡張し、移動、削除、追記、保存の一連フローを固定する | RT-62, RT-63, RT-45 | QEMU 上で基本編集コマンドの回帰を検知できる |

## M12: terminal / shell / vi の実用化と堅牢化

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-65 | `tests/Makefile` の rich terminal 系集約 test 依存を修正し、`make -C tests test` を再び green に戻す | RT-27, RT-63 | VT fixture を含む host test 一括実行が再度通る |
| [x] | RT-66 | `term` の main loop を見直し、無入力時の busy wait を減らす | RT-65 | idle 時の無駄な spin が減り、入力と描画の応答が維持される |
| [x] | RT-67 | framebuffer 経路でも viewport 変化を検出できるようにし、winsize 再通知と再描画を通す | RT-66 | graphics terminal でも resize 後に列数・行数が反映される |
| [x] | RT-68 | `eshell` に quote-aware tokenizer と最小 escape 処理を追加する | RT-40, RT-65 | quoted string を含む入力を token 単位で安全に解釈できる |
| [x] | RT-69 | `eshell` の parser / executor を拡張し、複数段 pipeline と `>>` を扱えるようにする | RT-68 | `cmd1 | cmd2 | cmd3` と `cmd >> file` が成立する |
| [x] | RT-70 | shell parser / I/O 合成の host/QEMU smoke test を拡張する | RT-69 | quoting、複数 pipeline、append redirection の回帰を検知できる |
| [x] | RT-71 | `vi` に alternate screen の入退場と終了時の画面復元を追加する | RT-64 | `vi` 終了後に shell 画面が自然に戻る |
| [x] | RT-72 | `vi` に undo/redo の最小履歴を追加する | RT-64, RT-71 | `u` / `Ctrl-R` で直前編集を戻したりやり直したりできる |
| [x] | RT-73 | `vi` に `/`, `?`, `n`, `N` の最小検索を追加する | RT-72 | 既存ファイル内を前方 / 後方検索できる |
| [x] | RT-74 | `vi` に char-wise / line-wise の最小 visual mode を追加する | RT-73 | 範囲選択と削除の基本操作が成立する |
| [x] | RT-75 | `vi` の host/QEMU smoke test を拡張し、alternate screen、undo/redo、検索、visual mode を固定する | RT-72, RT-73, RT-74 | 実用編集フローの回帰を自動検知できる |

## M13: 日本語 IME の漢字変換と候補 UI

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [x] | RT-76 | `ime_state` を拡張し、かな読み、候補列、選択 index、変換状態を保持できるようにする | RT-59, RT-66 | preedit と変換中候補を別状態として保持できる |
| [x] | RT-77 | 最小辞書 format と lookup 層を追加し、読みから候補列を引けるようにする | RT-76 | `かな` 読みから 1 件以上の候補を得られる |
| [x] | RT-78 | IME の変換 state machine を pure logic helper へ分離し、候補遷移と commit / cancel を `term` 非依存で扱えるようにする | RT-76, RT-77 | 変換開始、次候補、前候補、確定、キャンセル、`Backspace` 復帰を pure logic で扱える |
| [x] | RT-79 | `term` の入力経路に変換開始、候補移動、確定、キャンセル action を追加する | RT-78 | `Space` / `変換` / `Enter` / `Esc` で候補操作ができる |
| [x] | RT-80 | `term` overlay に候補 UI を追加し、preedit と候補一覧を描画する | RT-79, RT-67 | 候補選択中の状態が terminal 上で見える |
| [x] | RT-81 | shell と `vi` の両方で、候補確定済み UTF-8 を保存まで通す | RT-80, RT-75 | 漢字を含む入力が raw / canonical の両経路で破綻しない |
| [x] | RT-82 | IME 辞書 lookup と候補遷移の host test を追加する | RT-77, RT-78 | lookup、次候補、前候補、キャンセル、確定、`Backspace` 復帰を自動検知できる |
| [x] | RT-83 | IME 変換の QEMU smoke test を追加し、候補選択と保存を固定する | RT-81, RT-82 | 漢字変換の主要導線を QEMU 上で回帰検知できる |

## M14: フル IME 辞書と大規模候補対応

| 状態 | ID | タスク | 主な依存 | 完了条件 |
|---|---|---|---|---|
| [ ] | RT-84 | 採用する辞書 source、license、生成方針を確定し、build 入力形式を決める | RT-83 | 辞書更新元と配布上の扱いが明文化される |
| [ ] | RT-85 | source 辞書から compact blob を生成する tool と build 導線を追加する | RT-84 | text 辞書を runtime parse せず、image へ辞書 blob を載せられる |
| [ ] | RT-86 | on-disk blob lookup と small cache を pure logic helper として実装する | RT-85 | RAM 常駐量を抑えつつ読みから候補列を引ける |
| [ ] | RT-87 | `term` の IME 辞書層を blob lookup 前提へ置き換える | RT-86 | shell / `vi` が大規模辞書候補を実際に引ける |
| [ ] | RT-88 | 候補 UI を多候補 paging、切り詰め、ページ移動対応へ拡張する | RT-87 | 長い候補列でも選択操作が破綻しない |
| [ ] | RT-89 | 辞書 blob 欠落時の fallback と memory budget 診断を追加する | RT-87 | 辞書未搭載時も最小辞書で動き、cache 使用量を追える |
| [ ] | RT-90 | blob lookup / cache / memory budget の host test を追加する | RT-86, RT-89 | 大規模辞書ロジックの回帰を host test で検知できる |
| [ ] | RT-91 | 大規模辞書 IME の QEMU smoke test を追加し、代表語彙変換と保存を固定する | RT-88, RT-90 | 実機相当導線で大規模辞書 lookup の回帰を検知できる |

## 先送りする項目

- 複数 terminal セッション
- タブ補完や高度な line editing
- マウス入力
- ウィンドウシステム
- `vi` の複数バッファ、text object、マクロ
- 日本語 IME の予測変換、学習辞書
