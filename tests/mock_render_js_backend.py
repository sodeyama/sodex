#!/usr/bin/env python3
"""render_js backend の最小 mock。"""

from __future__ import annotations

import json
import sys


def main() -> int:
    payload = json.load(sys.stdin)
    url = payload.get("url", "http://example.invalid/")
    body = (
        "<html><head><title>Rendered Example</title></head>"
        "<body><main><p>rendered content from backend</p></main></body></html>"
    )
    json.dump(
        {
            "status": 200,
            "final_url": url,
            "content_type": "text/html",
            "body": body,
        },
        sys.stdout,
        ensure_ascii=False,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
