#!/usr/bin/env python3
"""
Mock HTTP server for Agent Transport Phase A testing.

Endpoints:
  GET  /healthz          -> {"status":"ok"}
  POST /echo             -> Echo request body with Content-Type: application/json
  POST /mock/claude      -> Claude API-like response (non-streaming)
  POST /mock/claude/error-> 429 with Retry-After header

Usage:
  python3 tests/mock_http_server.py [port]
  Default port: 8080
"""

import json
import sys
from http.server import HTTPServer, BaseHTTPRequestHandler


CLAUDE_MOCK_RESPONSE = {
    "id": "msg_mock_001",
    "type": "message",
    "role": "assistant",
    "content": [
        {
            "type": "text",
            "text": "Hello from mock Claude! This is a test response."
        }
    ],
    "model": "claude-sonnet-4-20250514",
    "stop_reason": "end_turn",
    "stop_sequence": None,
    "usage": {
        "input_tokens": 10,
        "output_tokens": 15
    }
}

CLAUDE_ERROR_RESPONSE = {
    "type": "error",
    "error": {
        "type": "rate_limit_error",
        "message": "Rate limit exceeded"
    }
}


class MockHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Log to stderr for debug
        sys.stderr.write("[mock-server] %s\n" % (format % args))

    def _send_json(self, status, body, extra_headers=None):
        data = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        if extra_headers:
            for k, v in extra_headers.items():
                self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/":
            html = (
                "<html><head><title>Sodex Mock</title></head>"
                "<body><h1>Hello from Sodex HTTP Client!</h1>"
                "<p>If you can read this, your OS just fetched HTML over TCP/IP.</p>"
                "</body></html>"
            ).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(html)))
            self.end_headers()
            self.wfile.write(html)
        elif self.path == "/healthz":
            self._send_json(200, {"status": "ok"})
        else:
            self._send_json(404, {"error": "not found"})

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length) if content_length > 0 else b""

        if self.path == "/echo":
            # Echo back the request body
            self.send_response(200)
            ct = self.headers.get("Content-Type", "application/octet-stream")
            self.send_header("Content-Type", ct)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        elif self.path == "/mock/claude":
            self._send_json(200, CLAUDE_MOCK_RESPONSE)

        elif self.path == "/mock/claude/error":
            self._send_json(429, CLAUDE_ERROR_RESPONSE,
                          extra_headers={"Retry-After": "30"})

        else:
            self._send_json(404, {"error": "not found"})


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    server = HTTPServer(("0.0.0.0", port), MockHandler)
    sys.stderr.write(f"[mock-server] Listening on 0.0.0.0:{port}\n")
    sys.stderr.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    server.server_close()
    sys.stderr.write("[mock-server] Stopped\n")


if __name__ == "__main__":
    main()
