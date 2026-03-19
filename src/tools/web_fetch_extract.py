#!/usr/bin/env python3
"""構造化 Web fetch 用の抽出ロジック。"""

from __future__ import annotations

from dataclasses import dataclass
from html.parser import HTMLParser
from typing import Any
from urllib.parse import urljoin
import json
import re


_BLOCK_TAGS = {
    "article",
    "aside",
    "blockquote",
    "dd",
    "div",
    "figcaption",
    "footer",
    "h1",
    "h2",
    "h3",
    "h4",
    "h5",
    "h6",
    "header",
    "li",
    "main",
    "nav",
    "ol",
    "p",
    "pre",
    "section",
    "td",
    "th",
    "ul",
}
_SKIP_TAGS = {"script", "style", "noscript", "template"}
_HIDDEN_STYLE_PATTERNS = (
    "display:none",
    "display: none",
    "visibility:hidden",
    "visibility: hidden",
)
_SPACE_RE = re.compile(r"\s+")
_JAPANESE_RE = re.compile(r"[\u3040-\u30ff\u3400-\u9fff]")


@dataclass
class ExtractedDocument:
    title: str
    excerpt: str
    main_text: str
    links: list[dict[str, str]]
    truncated: bool


def normalize_whitespace(text: str) -> str:
    return _SPACE_RE.sub(" ", text).strip()


def _clip_text(text: str, limit: int) -> tuple[str, bool]:
    if limit <= 0 or len(text) <= limit:
        return text, False

    clipped = text[:limit].rstrip()
    cut = max(clipped.rfind("\n\n"), clipped.rfind(". "), clipped.rfind("。"))
    if cut >= int(limit * 0.6):
        clipped = clipped[:cut].rstrip()
    return clipped, True


def _seems_useful(text: str) -> bool:
    length = len(text)
    if length >= 48:
        return True
    if _JAPANESE_RE.search(text) and length >= 14:
        return True
    if any(ch in text for ch in ".!?。！？") and length >= 18:
        return True
    return False


class _HTMLExtractor(HTMLParser):
    def __init__(self, base_url: str, max_links: int) -> None:
        super().__init__(convert_charrefs=True)
        self.base_url = base_url
        self.max_links = max_links
        self.in_title = False
        self.skip_depth = 0
        self.hidden_tags: list[str] = []
        self.title_parts: list[str] = []
        self.block_parts: list[str] = []
        self.blocks: list[str] = []
        self.links: list[dict[str, str]] = []
        self.link_stack: list[dict[str, Any]] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attr_map = {name.lower(): value for name, value in attrs}
        style = (attr_map.get("style") or "").lower()

        if tag in _SKIP_TAGS:
            self.skip_depth += 1
            return
        if "hidden" in attr_map or any(pattern in style for pattern in _HIDDEN_STYLE_PATTERNS):
            self.hidden_tags.append(tag)
        if tag == "title":
            self.in_title = True
            return
        if tag in _BLOCK_TAGS and self.block_parts:
            self._flush_block()
        if tag == "a" and len(self.links) < self.max_links:
            href = attr_map.get("href") or ""
            self.link_stack.append({"href": href, "parts": []})
        if tag == "br":
            self.block_parts.append("\n")

    def handle_endtag(self, tag: str) -> None:
        if tag in _SKIP_TAGS:
            if self.skip_depth > 0:
                self.skip_depth -= 1
            return
        if tag == "title":
            self.in_title = False
            return
        if tag == "a" and self.link_stack:
            entry = self.link_stack.pop()
            href = normalize_whitespace(entry["href"])
            text = normalize_whitespace("".join(entry["parts"]))
            if href and text and len(self.links) < self.max_links:
                self.links.append({
                    "href": urljoin(self.base_url, href),
                    "text": text,
                })
        if tag in _BLOCK_TAGS:
            self._flush_block()
        if self.hidden_tags and self.hidden_tags[-1] == tag:
            self.hidden_tags.pop()

    def handle_data(self, data: str) -> None:
        if not data or self.skip_depth > 0 or self.hidden_tags:
            return
        text = normalize_whitespace(data)
        if not text:
            return
        if self.in_title:
            self.title_parts.append(text)
            return
        self.block_parts.append(text)
        if self.link_stack:
            self.link_stack[-1]["parts"].append(text)

    def _flush_block(self) -> None:
        text = normalize_whitespace(" ".join(self.block_parts))
        self.block_parts = []
        if text:
            self.blocks.append(text)


def extract_html_document(
    html_text: str,
    *,
    base_url: str,
    max_chars: int,
    max_links: int,
) -> ExtractedDocument:
    parser = _HTMLExtractor(base_url=base_url, max_links=max_links)
    parser.feed(html_text)
    parser.close()

    title = normalize_whitespace(" ".join(parser.title_parts))
    deduped_blocks: list[str] = []
    seen: set[str] = set()
    for block in parser.blocks:
        if block not in seen:
            deduped_blocks.append(block)
            seen.add(block)

    useful_blocks = [block for block in deduped_blocks if _seems_useful(block)]
    if not useful_blocks:
        useful_blocks = deduped_blocks

    excerpt = useful_blocks[0] if useful_blocks else ""
    main_text = "\n\n".join(useful_blocks)
    main_text, truncated = _clip_text(main_text, max_chars)
    excerpt, excerpt_truncated = _clip_text(excerpt, min(max_chars, 280))
    return ExtractedDocument(
        title=title,
        excerpt=excerpt,
        main_text=main_text,
        links=parser.links[:max_links],
        truncated=truncated or excerpt_truncated,
    )


def extract_text_document(text: str, *, max_chars: int) -> ExtractedDocument:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    title = lines[0][:120] if lines else ""
    excerpt = lines[0] if lines else ""
    main_text, truncated = _clip_text(text.strip(), max_chars)
    excerpt, excerpt_truncated = _clip_text(excerpt, min(max_chars, 280))
    return ExtractedDocument(
        title=title,
        excerpt=excerpt,
        main_text=main_text,
        links=[],
        truncated=truncated or excerpt_truncated,
    )


def extract_json_document(text: str, *, max_chars: int) -> ExtractedDocument:
    title = ""
    try:
        parsed = json.loads(text)
        pretty = json.dumps(parsed, ensure_ascii=False, indent=2, sort_keys=True)
        if isinstance(parsed, dict):
            for key in ("title", "name", "headline"):
                value = parsed.get(key)
                if isinstance(value, str) and value.strip():
                    title = value.strip()
                    break
        text = pretty
    except json.JSONDecodeError:
        pass

    excerpt = normalize_whitespace(text[:280])
    main_text, truncated = _clip_text(text.strip(), max_chars)
    excerpt, excerpt_truncated = _clip_text(excerpt, min(max_chars, 280))
    return ExtractedDocument(
        title=title,
        excerpt=excerpt,
        main_text=main_text,
        links=[],
        truncated=truncated or excerpt_truncated,
    )
