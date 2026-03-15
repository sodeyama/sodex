# Plan 18: フル IME 辞書と大規模候補対応

## 概要

Plan 17 で guest 内 IME の最小漢字変換と候補 UI は成立した。
ただし現在の辞書は固定 table ベースで語彙が非常に少なく、
「基本的な漢字を広く変換する」「一般的な単語をそれなりに引ける」という段階には達していない。

この plan では IME を最小固定辞書から一段進め、
大規模辞書 blob、on-disk lookup、小さい RAM index/cache を持つ形へ拡張する。
初期ゴールは shell と `vi` で、基本語彙を広くカバーする候補提示を memory budget 内で成立させること。

## 依存と出口

- 依存: 17, `specs/memory-scaling`
- この plan の出口
  - build 時に辞書 source から compact blob を生成できる
  - `term` は辞書全体を RAM 常駐せず、on-disk lookup + 小さい cache で候補を引ける
  - shell と `vi` で基本語彙を広く変換できる
  - host test と QEMU smoke で lookup、候補ページング、memory budget を回帰検知できる

## 実装後の状態

- 辞書 source は Mozc `dictionary_oss` と手製補助語彙 TSV に確定した
- build 時に source TSV を再生成し、そこから compact blob を作る構成になっている
- runtime は on-disk lookup + small cache + fallback 辞書で候補を引く
- 候補 UI は page 単位で表示でき、host/QEMU の回帰 test も入っている
- 現在の生成規模は約 11 万読み / 14 万候補、blob は約 5.2MiB である

## 方針

- 辞書は `term` 常駐の巨大静的 table にしない
  - on-disk blob
  - 小さい RAM index
  - 小さい block cache
- 最初の対象は「基本語彙を広く引けること」に置く
  - 予測変換
  - 学習辞書
  - 文節分割や連文節変換
  は非ゴールにする
- lookup ロジックは引き続き pure helper 化する
  - `term` は action と描画に集中させる
- 辞書 source とライセンスを明確にする
  - Mozc `dictionary_oss` と手製補助語彙の組み合わせに固定する
  - build 再現性と配布時の取り扱いを曖昧にしない

## 設計判断

- 辞書 format は「読み bucket -> block offset 群 -> 候補列 blob」の形を基本にする
- runtime RAM budget は 1MB 未満を初期目標にする
  - index
  - cache
  - 候補展開 buffer
- 候補が多い場合は overlay 側で page 単位に送り、最初の数件だけ 1 行へ詰め込まない
- 辞書が見つからない場合は Plan 17 の最小固定辞書へ fallback できるようにする
- source 辞書は build 時に compile し、fs image に含める
  - runtime で raw text 辞書を parse しない
- Mozc 辞書はそのまま全件入れず、基本語彙寄りへ絞る
  - `cost <= 6000`
  - 候補に漢字を含む
  - 手製補助語彙にある読みは手製側を優先する

## 実装ステップ

1. 採用する辞書 source、license、生成パイプラインを決める
2. build 時に source 辞書から compact blob を生成する tool を追加する
3. on-disk blob を読む lookup helper と small cache を pure logic として実装する
4. `term` の IME 辞書層を blob lookup 前提へ差し替える
5. 候補 UI に多候補 paging と切り詰め方針を追加する
6. shell と `vi` で基本語彙の保存導線を確認する
7. host test で blob lookup、cache hit/miss、候補順、memory budget を固定する
8. QEMU smoke で大規模辞書の代表語彙変換と保存を固定する

## 変更対象

- 既存
  - `src/usr/include/ime.h`
  - `src/usr/lib/libc/ime_dictionary.c`
  - `src/usr/lib/libc/ime_conversion.c`
  - `src/usr/command/term.c`
  - `src/makefile`
  - `src/tools/mkfontpack.py`
  - `src/test/run_qemu_ime_smoke.py`
- 新規候補
  - `src/tools/mkimeblob.py`
  - `src/usr/include/ime_dict_blob.h`
  - `src/usr/lib/libc/ime_dict_blob.c`
  - `tests/test_ime_dict_blob.c`
  - `tests/test_ime_cache.c`
  - `src/test/data/term_ime_full_dictionary_reference.json`

## 検証

- 基本語彙の代表読みで複数候補が引ける
- 候補が多い読みで paging が動く
- RAM 常駐量が budget を超えない
- 辞書 blob 欠落時に最小固定辞書 fallback が働く
- shell と `vi` の両方で確定保存し、`cat` で再確認できる

## 完了条件

- guest 内 IME が「最小固定辞書」から「広い基本語彙を引ける辞書」へ進む
- memory scaling spec の headroom を実際の userland 機能へつなげられる
- 大規模辞書 lookup の回帰を host / QEMU で継続監視できる
