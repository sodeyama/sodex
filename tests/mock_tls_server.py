#!/usr/bin/env python3
"""
Mock TLS HTTPS server for Phase B testing.

Generates a self-signed certificate and runs an HTTPS server.
Endpoints are same as mock_http_server.py.

Usage:
  python3 tests/mock_tls_server.py [port]
  Default port: 4443
"""

import json
import os
import ssl
import subprocess
import sys
import tempfile
from http.server import HTTPServer, BaseHTTPRequestHandler


CLAUDE_MOCK_RESPONSE = {
    "id": "msg_mock_001",
    "type": "message",
    "role": "assistant",
    "content": [
        {
            "type": "text",
            "text": "Hello from mock TLS Claude!"
        }
    ],
    "model": "claude-sonnet-4-20250514",
    "stop_reason": "end_turn",
    "stop_sequence": None,
    "usage": {"input_tokens": 10, "output_tokens": 8}
}


class MockTLSHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        sys.stderr.write("[mock-tls-server] %s\n" % (format % args))

    def _send_json(self, status, body):
        data = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/healthz":
            self._send_json(200, {"status": "ok", "tls": True})
        elif self.path == "/":
            html = b"<html><body><h1>TLS OK</h1></body></html>"
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(html)))
            self.end_headers()
            self.wfile.write(html)
        else:
            self._send_json(404, {"error": "not found"})

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length) if content_length > 0 else b""

        if self.path == "/mock/claude":
            self._send_json(200, CLAUDE_MOCK_RESPONSE)
        elif self.path == "/echo":
            ct = self.headers.get("Content-Type", "application/octet-stream")
            self.send_response(200)
            self.send_header("Content-Type", ct)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self._send_json(404, {"error": "not found"})


def generate_self_signed_cert(certfile, keyfile):
    """Generate a self-signed certificate using openssl."""
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "ec",
        "-pkeyopt", "ec_paramgen_curve:prime256v1",
        "-keyout", keyfile, "-out", certfile,
        "-days", "1", "-nodes",
        "-subj", "/CN=localhost",
        "-addext", "subjectAltName=IP:10.0.2.2,IP:127.0.0.1,DNS:localhost",
    ], check=True, capture_output=True)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 4443

    # Generate self-signed cert
    tmpdir = tempfile.mkdtemp()
    certfile = os.path.join(tmpdir, "cert.pem")
    keyfile = os.path.join(tmpdir, "key.pem")
    generate_self_signed_cert(certfile, keyfile)
    sys.stderr.write(f"[mock-tls-server] Generated cert: {certfile}\n")

    # Create HTTPS server
    server = HTTPServer(("0.0.0.0", port), MockTLSHandler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile, keyfile)
    # Force TLS 1.2 only (BearSSL doesn't support TLS 1.3)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2
    server.socket = ctx.wrap_socket(server.socket, server_side=True)

    sys.stderr.write(f"[mock-tls-server] Listening on 0.0.0.0:{port} (TLS)\n")
    sys.stderr.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    server.server_close()
    sys.stderr.write("[mock-tls-server] Stopped\n")


if __name__ == "__main__":
    main()
