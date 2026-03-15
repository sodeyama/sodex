#!/usr/bin/env python3
"""TrueType フォントから端末用ビットマップヘッダを生成する。"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ASCII_FIRST = 32
ASCII_LAST = 126
RESAMPLE_LANCZOS = getattr(Image, "Resampling", Image).LANCZOS
PUNCTUATION_SCALE_OVERRIDES = {
    0x3001: 1.35,
    0x3002: 1.35,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="TTF/OTF から 8x16 の font8x16_data.h を生成する"
    )
    parser.add_argument("--font-path", required=True, help="入力フォントファイル")
    parser.add_argument("--output-header", required=True, help="生成先ヘッダ")
    parser.add_argument("--font-size", type=int, default=12, help="Pillow に渡すフォントサイズ")
    parser.add_argument("--cell-width", type=int, default=8, help="セル幅")
    parser.add_argument("--cell-height", type=int, default=16, help="セル高さ")
    parser.add_argument("--threshold", type=int, default=96, help="2値化しきい値")
    parser.add_argument(
        "--supersample",
        type=int,
        default=1,
        help="高解像度で一度描いて縮小する倍率",
    )
    parser.add_argument(
        "--codepoint-ranges",
        default="",
        help="map 出力時の codepoint range。例: 3000-303f,3040-30ff",
    )
    parser.add_argument(
        "--symbol-name",
        default="font16x16_glyphs",
        help="map 出力時の配列シンボル名",
    )
    parser.add_argument(
        "--source-name",
        default="UDEVGothic-Regular.ttf",
        help="生成コメントへ埋め込むソース名",
    )
    parser.add_argument(
        "--text-source",
        action="append",
        default=[],
        help="追加 glyph 抽出元の UTF-8 text file",
    )
    return parser.parse_args()


def emphasize_small_punctuation(image: Image.Image,
                                codepoint: int,
                                cell_width: int,
                                cell_height: int) -> Image.Image:
    scale = PUNCTUATION_SCALE_OVERRIDES.get(codepoint)
    bbox: tuple[int, int, int, int] | None
    crop: Image.Image
    target_width: int
    target_height: int
    margin_bottom: int
    x: int
    y: int

    if scale is None:
        return image

    bbox = image.getbbox()
    if bbox is None:
        return image

    crop = image.crop(bbox)
    target_width = min(cell_width, max(crop.width, int(round(crop.width * scale))))
    target_height = min(cell_height, max(crop.height, int(round(crop.height * scale))))
    if target_width == crop.width and target_height == crop.height:
        return image

    margin_bottom = cell_height - bbox[3]
    x = bbox[0]
    y = cell_height - margin_bottom - target_height
    y = max(0, y)
    if x + target_width > cell_width:
        x = cell_width - target_width

    adjusted = Image.new("L", (cell_width, cell_height), 0)
    adjusted.paste(crop.resize((target_width, target_height), RESAMPLE_LANCZOS), (x, y))
    return adjusted


def render_glyph(font: ImageFont.FreeTypeFont, codepoint: int,
                 cell_width: int, cell_height: int,
                 advance_width: int, baseline: int,
                 threshold: int,
                 supersample: int) -> list[int]:
    ch = chr(codepoint)
    render_width = cell_width * supersample
    render_height = cell_height * supersample
    image = Image.new("L", (render_width, render_height), 0)
    draw = ImageDraw.Draw(image)
    origin_x = max(0, (render_width - advance_width) // 2)

    # 各文字を個別に中央寄せすると "." や "g" が不自然に浮くため、
    # フォントの advance と baseline を全 glyph で共有する。
    draw.text((origin_x, baseline), ch, fill=255, font=font, anchor="ls")
    if supersample > 1:
        image = image.resize((cell_width, cell_height), RESAMPLE_LANCZOS)
    image = emphasize_small_punctuation(image, codepoint, cell_width, cell_height)
    image = image.point(lambda value: 255 if value >= threshold else 0)

    pixels = image.load()
    rows: list[int] = []
    for y in range(cell_height):
        row_bits = 0
        for x in range(cell_width):
            if pixels[x, y] != 0:
                row_bits |= 1 << (cell_width - 1 - x)
        rows.append(row_bits)
    return rows


def parse_codepoint_ranges(spec: str) -> list[int]:
    seen: set[int] = set()

    if not spec:
        return []

    for part in spec.split(","):
        item = part.strip()
        if not item:
            continue
        if "-" in item:
            first_text, last_text = item.split("-", 1)
            first = int(first_text, 16)
            last = int(last_text, 16)
        else:
            first = int(item, 16)
            last = first
        if last < first:
            first, last = last, first
        for codepoint in range(first, last + 1):
            seen.add(codepoint)
    return sorted(seen)


def load_text_source_codepoints(paths: list[str]) -> list[int]:
    seen: set[int] = set()

    for raw_path in paths:
        path = Path(raw_path)
        for ch in path.read_text(encoding="utf-8"):
            codepoint = ord(ch)
            if codepoint < 0x80:
                continue
            seen.add(codepoint)

    return sorted(seen)


def format_row_values(rows: list[int], row_hex_width: int) -> str:
    return ", ".join(f"0x{value:0{row_hex_width}x}" for value in rows)


def header_guard(path: Path) -> str:
    chars = []

    for ch in path.name.upper():
        if ("A" <= ch <= "Z") or ("0" <= ch <= "9"):
            chars.append(ch)
        else:
            chars.append("_")
    return "_" + "".join(chars)


def build_header(source_name: str,
                 output_path: Path,
                 cell_width: int,
                 cell_height: int,
                 glyphs: list[tuple[int, list[int]]]) -> str:
    row_hex_width = (cell_width + 3) // 4
    lines = [
        f"#ifndef {header_guard(output_path)}",
        f"#define {header_guard(output_path)}",
        "",
        f"/* Generated by src/tools/mkfontpack.py from {source_name}. */",
        "",
        f"#define FONT8X16_WIDTH {cell_width}",
        f"#define FONT8X16_HEIGHT {cell_height}",
        "",
        "static const unsigned int font8x16_printable[95][FONT8X16_HEIGHT] = {",
    ]

    for codepoint, rows in glyphs:
        ch = chr(codepoint)
        if ch == "\\":
            label = "'\\\\'"
        elif ch == "'":
            label = "'\\''"
        else:
            label = f"'{ch}'"
        lines.append(f"  /* {label} */ {{{format_row_values(rows, row_hex_width)}}},")

    lines.extend([
        "};",
        "",
        f"#endif /* {header_guard(output_path)} */",
        "",
    ])
    return "\n".join(lines)


def build_map_header(source_name: str,
                     output_path: Path,
                     symbol_name: str,
                     cell_width: int,
                     cell_height: int,
                     glyphs: list[tuple[int, list[int]]]) -> str:
    row_hex_width = (cell_width + 3) // 4
    lines = [
        f"#ifndef {header_guard(output_path)}",
        f"#define {header_guard(output_path)}",
        "",
        f"/* Generated by src/tools/mkfontpack.py from {source_name}. */",
        "",
        f"#define FONT16X16_WIDTH {cell_width}",
        f"#define FONT16X16_HEIGHT {cell_height}",
        "",
        "struct font16x16_entry {",
        "  unsigned int codepoint;",
        "  unsigned int rows[FONT16X16_HEIGHT];",
        "};",
        "",
        f"static const struct font16x16_entry {symbol_name}[] = {{",
    ]

    for codepoint, rows in glyphs:
        lines.append(
            f"  {{0x{codepoint:04x}, {{{format_row_values(rows, row_hex_width)}}}}},"
        )

    lines.extend([
        "};",
        "",
        f"#define FONT16X16_GLYPH_COUNT {len(glyphs)}",
        "",
        f"#endif /* {header_guard(output_path)} */",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    args = parse_args()

    font_path = Path(args.font_path)
    output_header = Path(args.output_header)
    if not font_path.exists():
        print(f"フォントが見つかりません: {font_path}", file=sys.stderr)
        return 1

    codepoints = set(parse_codepoint_ranges(args.codepoint_ranges))
    codepoints.update(load_text_source_codepoints(args.text_source))
    if args.supersample <= 0:
        print("supersample は 1 以上である必要があります", file=sys.stderr)
        return 1

    font = ImageFont.truetype(str(font_path), size=args.font_size * args.supersample)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    top_padding = max(0, (args.cell_height * args.supersample - line_height) // 2)
    baseline = top_padding + ascent
    advance_width = 0
    if codepoints:
        sample_codepoints = sorted(codepoints)
    else:
        sample_codepoints = list(range(ASCII_FIRST, ASCII_LAST + 1))
    for codepoint in sample_codepoints:
        advance_width = max(advance_width,
                            int(round(font.getlength(chr(codepoint)))))
    glyphs: list[tuple[int, list[int]]] = []
    for codepoint in sample_codepoints:
        glyphs.append(
            (
                codepoint,
                render_glyph(
                    font,
                    codepoint,
                    args.cell_width,
                    args.cell_height,
                    advance_width,
                    baseline,
                    args.threshold,
                    args.supersample,
                ),
            )
        )

    output_header.parent.mkdir(parents=True, exist_ok=True)
    if codepoints:
        text = build_map_header(
            source_name=args.source_name,
            output_path=output_header,
            symbol_name=args.symbol_name,
            cell_width=args.cell_width,
            cell_height=args.cell_height,
            glyphs=glyphs,
        )
    else:
        text = build_header(
            source_name=args.source_name,
            output_path=output_header,
            cell_width=args.cell_width,
            cell_height=args.cell_height,
            glyphs=glyphs,
        )
    output_header.write_text(text, encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
