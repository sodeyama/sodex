#!/usr/bin/env python3
"""webfetch smoke 用の source server。"""

from __future__ import annotations

from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import sys


LONG_TEXT = " ".join(
    [f"long paragraph {index:02d} keeps the extraction path busy." for index in range(1, 40)]
)


class MockWebFetchSourceHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("[mock-webfetch-source] %s\n" % (fmt % args))

    def _send_bytes(self, status: int, content_type: str, body: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path == "/article":
            body = (
                "<html><head><title>Sodex Article</title></head>"
                "<body><nav>nav noise</nav>"
                "<main><article>"
                "<h1>Sodex Article</h1>"
                "<p>This article explains the structured fetch pipeline.</p>"
                "<p>The second paragraph contains the main text for smoke validation.</p>"
                "<script>ignored()</script>"
                "<p hidden>hidden text</p>"
                "<a href=\"/about\">About</a>"
                "</article></main></body></html>"
            ).encode("utf-8")
            self._send_bytes(200, "text/html; charset=utf-8", body)
            return

        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/article")
            self.end_headers()
            return

        if self.path == "/long":
            body = (
                "<html><head><title>Long Content</title></head>"
                "<body><main><p>%s</p></main></body></html>" % LONG_TEXT
            ).encode("utf-8")
            self._send_bytes(200, "text/html; charset=utf-8", body)
            return

        if self.path == "/plain":
            body = b"plain title\nplain body line\n"
            self._send_bytes(200, "text/plain; charset=utf-8", body)
            return

        if self.path == "/json":
            body = json.dumps(
                {
                    "title": "Weather JSON",
                    "summary": "Cloudy with light wind",
                    "temperature_c": 18,
                }
            ).encode("utf-8")
            self._send_bytes(200, "application/json", body)
            return

        if self.path == "/weather/tokyo":
            body = (
                "<html><head><title>Tokyo Weather 2026-03-19</title></head>"
                "<body><main><article>"
                "<h1>Tokyo Weather for 2026-03-19</h1>"
                "<p>Today in Tokyo is sunny with a high of 19C and a low of 8C.</p>"
                "<p>Humidity is moderate and the wind is light from the north.</p>"
                "</article></main></body></html>"
            ).encode("utf-8")
            self._send_bytes(200, "text/html; charset=utf-8", body)
            return

        self._send_bytes(404, "text/plain; charset=utf-8", b"not found")


def main() -> int:
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 18081
    server = HTTPServer(("0.0.0.0", port), MockWebFetchSourceHandler)
    sys.stderr.write(f"[mock-webfetch-source] listening on 0.0.0.0:{port}\n")
    sys.stderr.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
