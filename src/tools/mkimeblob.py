#!/usr/bin/env python3
"""IME 辞書 source から compact blob を生成する。"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

HEADER_STRUCT = struct.Struct("<4s7I")
ENTRY_STRUCT = struct.Struct("<IIHHI")
MAGIC = b"IMED"
MIN_BUCKETS = 64
MAX_BUCKETS = 16384
TARGET_ENTRIES_PER_BUCKET = 16


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="入力 TSV")
    parser.add_argument("--output", required=True, type=Path, help="出力 blob")
    parser.add_argument(
        "--bucket-count",
        type=int,
        default=0,
        help="bucket 数。0 なら entry 数から自動決定",
    )
    return parser.parse_args()


def load_entries(path: Path) -> dict[str, list[str]]:
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
        if not reading or not candidate:
            raise ValueError(f"{path}:{lineno}: 空のよみ/候補は使えません")
        grouped.setdefault(reading, []).append(candidate)

    if not grouped:
        raise ValueError(f"{path}: 辞書エントリがありません")
    return grouped


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def next_power_of_two(value: int) -> int:
    result = 1

    while result < value:
        result <<= 1
    return result


def choose_bucket_count(entry_count: int, requested: int) -> int:
    if requested > 0:
        return requested

    target = max(MIN_BUCKETS, entry_count // TARGET_ENTRIES_PER_BUCKET)
    target = min(MAX_BUCKETS, target)
    return max(MIN_BUCKETS, min(MAX_BUCKETS, next_power_of_two(target)))


def build_blob(grouped: dict[str, list[str]], bucket_count: int) -> bytes:
    entries: list[tuple[int, bytes, list[str]]] = []
    bucket_sizes = [0] * bucket_count

    for reading, candidates in grouped.items():
        reading_bytes = reading.encode("utf-8")
        bucket = fnv1a(reading_bytes) % bucket_count
        entries.append((bucket, reading_bytes, candidates))
        bucket_sizes[bucket] += 1

    entries.sort(key=lambda item: (item[0], item[1]))

    bucket_offsets = [0] * (bucket_count + 1)
    running = 0
    for index, size in enumerate(bucket_sizes):
        bucket_offsets[index] = running
        running += size
    bucket_offsets[bucket_count] = running

    data_blob = bytearray()
    entry_blob = bytearray()
    for _bucket, reading_bytes, candidates in entries:
        reading_offset = len(data_blob)
        data_blob.extend(reading_bytes)

        candidate_offset = len(data_blob)
        candidate_bytes = 0
        for candidate in candidates:
            encoded = candidate.encode("utf-8") + b"\0"
            data_blob.extend(encoded)
            candidate_bytes += len(encoded)

        entry_blob.extend(
            ENTRY_STRUCT.pack(
                reading_offset,
                candidate_offset,
                len(reading_bytes),
                len(candidates),
                candidate_bytes,
            )
        )

    bucket_blob = struct.pack(f"<{len(bucket_offsets)}I", *bucket_offsets)
    bucket_offset = HEADER_STRUCT.size
    entry_offset = bucket_offset + len(bucket_blob)
    data_offset = entry_offset + len(entry_blob)
    file_size = data_offset + len(data_blob)

    header = HEADER_STRUCT.pack(
        MAGIC,
        1,
        bucket_count,
        len(entries),
        bucket_offset,
        entry_offset,
        data_offset,
        file_size,
    )
    return header + bucket_blob + entry_blob + data_blob


def main() -> int:
    args = parse_args()

    grouped = load_entries(args.input)
    bucket_count = choose_bucket_count(len(grouped), args.bucket_count)
    if bucket_count <= 0 or bucket_count > MAX_BUCKETS:
        raise ValueError(f"bucket-count は 1..{MAX_BUCKETS} で指定してください")

    blob = build_blob(grouped, bucket_count)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)

    print(
        f"generated {args.output} entries={len(grouped)} "
        f"bucket_count={bucket_count} bytes={len(blob)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
