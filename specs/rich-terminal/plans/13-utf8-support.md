# Plan 13: UTF-8 と多言語表示

## 概要

ASCII 前提で成立させた terminal / shell / `vi` を、
UTF-8 と多言語テキストを破綻なく扱える構成へ拡張する。
初期ゴールは `UDEV Gothic` から生成した既定フォントパックを kernel image に載せ、
日本語を含む UTF-8 ファイルを表示・編集・保存できること。

## 依存と出口

- 依存: 03, 07, 08, 09, 12
- この plan の出口
  - terminal が UTF-8 バイト列を Unicode scalar value と表示幅へ変換できる
  - kernel が起動時に既定フォントパックを読み、early console と framebuffer backend が同じ glyph を使える
  - `term` が kernel 既定フォントを使って多言語 glyph を描画できる
  - `vi` が日本語を含む UTF-8 テキストを表示・編集・保存できる
  - フォント差し替えと glyph fallback の方針が入り、多言語表示の拡張点が揃う

## 方針

- まずは UTF-8 の decode / encode と表示幅計算を安定させる
- 内部表現は「バイト列」ではなく「Unicode scalar value + 表示幅」に寄せる
- renderer は固定幅セルを維持しつつ、幅 0 / 1 / 2 を扱えるようにする
- 不正な UTF-8 は置換文字か代替 glyph へ落とし、描画系を壊さない
- host build では `UDEV Gothic` を入力フォントとして使い、
  ASCII と日本語を含む subset 済み glyph を生成する
- kernel は TrueType を直接読まず、host build で生成したビットマップの
  `font_default` パックを起動時に既定ロードする
- `term` は kernel が公開する既定フォントメタデータを使い、
  boot 直後の console と同じ見た目で描画する

## 設計判断

- VT parser 自体は byte stream を読む
  - ただし printable 領域へ出す前に UTF-8 decoder を通す
- `term_cell.ch` は最終的に Unicode scalar value を保持できる形へ拡張する
  - 1 byte ASCII 前提のまま wide char を無理に詰め込まない
- 文字幅は `wcwidth` 相当の最小表を in-tree で持つ
  - East Asian Wide / Fullwidth、結合文字、制御文字を区別する
- `vi` のカーソル移動と行編集は「バイト数」ではなく「文字境界」と「表示幅」で扱う
- kernel に載せるのは生の TTC/TTF ではなく、subset 済み bitmap font pack とする
  - ライセンス上の扱いを軽くしつつ、kernel 側の実装を単純に保つ
- 元フォントには `UDEV Gothic` を使う
  - kernel に入れる生成物名は `UDEV Gothic` そのものではなく、別名の `font_default` として扱う
  - 配布時は OFL 1.1 のライセンス文書と attribution を同梱する
- 既定フォントは 2 段構成にする
  - 半角 1-cell は `UDEV Gothic` 由来の 8x16 glyph
  - 全角 2-cell は `UDEV Gothic` 由来の 16x16 glyph
- userland renderer は独自フォントを持たず、まず kernel 既定フォントを使う
  - 必要になった段階で差し替え API を追加する
- セルサイズ変更は後段階に分ける
  - まず 8x16 / 16x16 の固定組み合わせで成立させる
  - 次に必要なら `cols` / `rows` 再計算と font swap へ広げる

## 実装ステップ

1. UTF-8 decoder / encoder と不正シーケンス処理を pure logic として追加する
2. Unicode scalar value ごとの表示幅計算を追加する
3. terminal surface / renderer を幅 0 / 1 / 2 文字に対応させる
4. `mkfontpack` を追加し、`UDEV Gothic` から subset 済み bitmap の
   `font_default` パックを生成できるようにする
5. build で `font_default` を kernel image に埋め込み、boot 時に font registry へ既定ロードする
6. glyph lookup と fallback を kernel / `term` の両方で `font_default` 前提に整理する
7. shell 表示、`cat`、`ls`、prompt 描画で UTF-8 を破綻なく通す
8. `vi` の buffer、カーソル、Backspace、行分割、再描画を UTF-8 / wide char 対応にする
9. host test と QEMU smoke で日本語ファイルの表示・編集・保存と既定フォント描画を固定する
10. 配布時に OFL 1.1 文書と attribution を同梱する導線を整える

## 変更対象

- 既存
  - `src/display/fb_backend.c`
  - `src/include/font8x16.h`
  - `src/usr/lib/libc/vt_parser.c`
  - `src/usr/lib/libc/terminal_surface.c`
  - `src/usr/lib/libc/cell_renderer.c`
  - `src/usr/lib/libc/vi_buffer.c`
  - `src/usr/lib/libc/vi_screen.c`
  - `src/usr/command/term.c`
  - `src/usr/command/vi.c`
  - `src/makefile`
- 新規候補
  - `src/tools/mkfontpack.py`
  - `src/include/font_pack.h`
  - `src/include/font_registry.h`
  - `src/display/font_registry.c`
  - `src/include/generated/font_default.h`
  - `third_party/fonts/README.md`
  - `third_party/fonts/OFL-UDEV-Gothic.txt`
  - `src/usr/lib/libc/utf8.c`
  - `src/usr/include/utf8.h`
  - `src/usr/lib/libc/wcwidth.c`
  - `src/usr/include/wcwidth.h`
  - `src/include/font16x16.h`
  - `tests/test_utf8.c`
  - `tests/test_wcwidth.c`
  - `tests/test_font_registry.c`
  - `src/test/run_qemu_utf8_smoke.py`

## 検証

- `cat utf8.txt` で日本語を含む UTF-8 テキストを表示できる
- `vi utf8.txt` で日本語を含む行を開き、追記して `:wq` で保存できる
- wide char を含む行でカーソル位置と Backspace が破綻しない
- 不正 UTF-8 を読んでも terminal がハングしない
- boot 直後の kernel 表示と `term` の両方で、同じ既定フォントパックから glyph が描画される
- `font_default` 生成後に再 build すると `UDEV Gothic` 由来の半角文字と日本語 glyph が描画できる
- リポジトリ内に OFL 1.1 文書と attribution の置き場があり、配布時に欠落しない

## 完了条件

- shell / `cat` / `vi` で UTF-8 テキストを実用上扱える
- terminal が文字境界と表示幅を意識して再描画できる
- `font_default` が kernel image に含まれ、boot 時に既定フォントとして読まれる
- `UDEV Gothic` 由来の半角 glyph と日本語 glyph で多言語表示を始められる
- OFL 1.1 のライセンス文書と attribution を添えて配布できる
