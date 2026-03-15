# Mozc dictionary source

- Upstream: `google/mozc`
- Revision: `cc288ecf425b98bf71e757defb34d9f1f5bc2733`
- Source path: `src/data/dictionary_oss/`

この directory には、IME 用大規模辞書 source を build するために
Mozc の open source dictionary を同梱している。

Sodex 側では build 時に `src/tools/build_ime_dictionary_source.py` が
以下を行う。

- 手製補助語彙 `src/tools/ime_dictionary_manual.tsv` を優先する
- Mozc dictionary から、ひらがな読みで扱える entry を抽出する
- `cost <= 6000` かつ候補に漢字を含む entry へ絞り、基本語彙寄りに保つ
- 読みごとに重複候補を除去し、候補数と候補 bytes を runtime 制約内へ切り詰める

ライセンス文面は同梱の [LICENSE](LICENSE) と
[README.upstream.txt](README.upstream.txt) を参照。
