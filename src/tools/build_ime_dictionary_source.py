#!/usr/bin/env python3
"""手製補助語彙と Mozc 辞書から IME 用 source TSV を生成する。"""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manual", required=True, type=Path, help="手製補助語彙 TSV")
    parser.add_argument("--mozc-dir", required=True, type=Path, help="Mozc dictionary_oss dir")
    parser.add_argument("--output", required=True, type=Path, help="生成先 TSV")
    parser.add_argument("--max-candidates", type=int, default=16, help="1 読みあたり最大候補数")
    parser.add_argument(
        "--max-candidate-bytes",
        type=int,
        default=1024,
        help="1 読みあたり候補 UTF-8 総 bytes 上限",
    )
    parser.add_argument(
        "--max-reading-bytes",
        type=int,
        default=63,
        help="読み UTF-8 bytes 上限",
    )
    parser.add_argument(
        "--max-cost",
        type=int,
        default=6000,
        help="Mozc 候補に許可する最大 cost",
    )
    return parser.parse_args()


def load_manual_entries(path: Path) -> dict[str, list[str]]:
    grouped: dict[str, list[str]] = {}

    for lineno, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        parts = raw_line.split("\t")
        if len(parts) != 2:
            raise ValueError(f"{path}:{lineno}: <よみ><TAB><候補> を期待しました")
        reading = parts[0].strip()
        candidate = parts[1].strip()
        grouped.setdefault(reading, []).append(candidate)

    if not grouped:
        raise ValueError(f"{path}: 手製補助語彙が空です")
    return grouped


def is_hiragana_text(text: str) -> bool:
    if not text:
        return False
    for ch in text:
        if ch in {"ー", "ゝ", "ゞ"}:
            continue
        if "ぁ" <= ch <= "ゖ" or ch == "ゔ":
            continue
        return False
    return True


def is_ascii_text(text: str) -> bool:
    if not text:
        return False
    return all(ord(ch) < 0x80 for ch in text)


def has_kanji(text: str) -> bool:
    return any("\u4e00" <= ch <= "\u9fff" for ch in text)


def is_supported_reading(reading: str, max_reading_bytes: int) -> bool:
    if not is_hiragana_text(reading):
        return False
    if len(reading.encode("utf-8")) > max_reading_bytes:
        return False
    return True


def is_useful_candidate(reading: str, candidate: str) -> bool:
    if not candidate:
        return False
    if candidate == reading:
        return False
    if is_ascii_text(candidate):
        return False
    if is_hiragana_text(candidate):
        return False
    if not has_kanji(candidate):
        return False
    return True


def load_mozc_entries(
    mozc_dir: Path,
    manual_readings: set[str],
    max_candidates: int,
    max_candidate_bytes: int,
    max_reading_bytes: int,
    max_cost: int,
) -> dict[str, list[str]]:
    grouped: dict[str, list[tuple[int, int, str]]] = defaultdict(list)
    order = 0

    for path in sorted(mozc_dir.glob("dictionary*.txt")):
        for lineno, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            line = raw_line.strip()
            parts: list[str]
            reading: str
            cost: int
            candidate: str

            if not line or line.startswith("#"):
                continue
            parts = raw_line.split("\t")
            if len(parts) < 5:
                raise ValueError(f"{path}:{lineno}: Mozc 辞書 format が不正です")

            reading = parts[0].strip()
            if reading in manual_readings:
                continue
            if not is_supported_reading(reading, max_reading_bytes):
                continue

            candidate = parts[4].strip()
            if not is_useful_candidate(reading, candidate):
                continue

            try:
                cost = int(parts[3])
            except ValueError as exc:
                raise ValueError(f"{path}:{lineno}: cost が不正です: {parts[3]!r}") from exc
            if cost > max_cost:
                continue

            grouped[reading].append((cost, order, candidate))
            order += 1

    finalized: dict[str, list[str]] = {}
    for reading in sorted(grouped):
        seen: set[str] = set()
        selected: list[str] = []
        total_bytes = 0
        items = sorted(grouped[reading], key=lambda item: (item[0], item[1], item[2]))

        for _cost, _order, candidate in items:
            candidate_bytes = len(candidate.encode("utf-8")) + 1

            if candidate in seen:
                continue
            if candidate_bytes > max_candidate_bytes:
                continue
            if len(selected) >= max_candidates:
                break
            if total_bytes + candidate_bytes > max_candidate_bytes:
                break

            selected.append(candidate)
            seen.add(candidate)
            total_bytes += candidate_bytes

        if selected:
            finalized[reading] = selected

    return finalized


def write_output(path: Path, manual_entries: dict[str, list[str]], mozc_entries: dict[str, list[str]]) -> None:
    lines: list[str] = [
        "# IME 辞書 source",
        "# 手製補助語彙: CC0-1.0",
        "# 外部辞書: third_party/dictionaries/mozc を参照",
        "# 形式: よみ<TAB>候補",
        "",
        "# 手製補助語彙",
    ]

    for reading, candidates in manual_entries.items():
        for candidate in candidates:
            lines.append(f"{reading}\t{candidate}")

    lines.extend(["", "# Mozc 辞書由来語彙"])
    for reading in sorted(mozc_entries):
        for candidate in mozc_entries[reading]:
            lines.append(f"{reading}\t{candidate}")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()

    if args.max_candidates <= 0:
        raise ValueError("max-candidates は 1 以上で指定してください")
    if args.max_candidate_bytes <= 0:
        raise ValueError("max-candidate-bytes は 1 以上で指定してください")
    if args.max_reading_bytes <= 0:
        raise ValueError("max-reading-bytes は 1 以上で指定してください")
    if args.max_cost < 0:
        raise ValueError("max-cost は 0 以上で指定してください")

    manual_entries = load_manual_entries(args.manual)
    mozc_entries = load_mozc_entries(
        args.mozc_dir,
        set(manual_entries),
        args.max_candidates,
        args.max_candidate_bytes,
        args.max_reading_bytes,
        args.max_cost,
    )
    write_output(args.output, manual_entries, mozc_entries)

    total_entries = sum(len(candidates) for candidates in manual_entries.values())
    total_entries += sum(len(candidates) for candidates in mozc_entries.values())
    print(
        f"generated {args.output} readings={len(manual_entries) + len(mozc_entries)} "
        f"entries={total_entries}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
