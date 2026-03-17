#!/usr/bin/env python3
"""
Mock Claude API server with TLS + SSE streaming.

Endpoints:
  POST /v1/messages  -> SSE streaming response (like Claude Messages API)
  GET  /healthz      -> {"status":"ok"}

Scenarios (via request body):
  - Default text: SSE stream with "Hello from mock Claude!"
  - With tools in request: SSE stream with tool_use block
  - Special prompt "error_test": SSE stream with error event

Usage:
  python3 tests/mock_claude_server.py [port] [--tls]
  Default port: 4443, TLS enabled by default

TLS cert generation (if certs don't exist):
  openssl req -new -newkey rsa:2048 -x509 -sha256 -days 365 -nodes \
    -out tests/certs/cert.pem -keyout tests/certs/key.pem -subj "/CN=localhost"
"""

import json
import os
import ssl
import sys
import time
from http.server import HTTPServer, BaseHTTPRequestHandler


def sse_event(event_name, data):
    """Format an SSE event."""
    return f"event: {event_name}\ndata: {json.dumps(data)}\n\n"


def text_response_stream():
    """Generate SSE events for a simple text response."""
    events = []

    events.append(sse_event("message_start", {
        "type": "message_start",
        "message": {
            "id": "msg_mock_001",
            "type": "message",
            "role": "assistant",
            "content": [],
            "model": "mock-claude",
            "stop_reason": None,
            "usage": {"input_tokens": 10, "output_tokens": 1}
        }
    }))

    events.append(sse_event("content_block_start", {
        "type": "content_block_start",
        "index": 0,
        "content_block": {"type": "text", "text": ""}
    }))

    events.append(sse_event("ping", {"type": "ping"}))

    # Send text in chunks
    for word in ["Hello", " from", " mock", " Claude!"]:
        events.append(sse_event("content_block_delta", {
            "type": "content_block_delta",
            "index": 0,
            "delta": {"type": "text_delta", "text": word}
        }))

    events.append(sse_event("content_block_stop", {
        "type": "content_block_stop",
        "index": 0
    }))

    events.append(sse_event("message_delta", {
        "type": "message_delta",
        "delta": {"stop_reason": "end_turn", "stop_sequence": None},
        "usage": {"output_tokens": 5}
    }))

    events.append(sse_event("message_stop", {"type": "message_stop"}))

    return events


def tool_use_response_stream():
    """Generate SSE events for a tool_use response."""
    events = []

    events.append(sse_event("message_start", {
        "type": "message_start",
        "message": {
            "id": "msg_mock_tool",
            "type": "message",
            "role": "assistant",
            "content": [],
            "model": "mock-claude",
            "stop_reason": None,
            "usage": {"input_tokens": 20, "output_tokens": 1}
        }
    }))

    # Text block first
    events.append(sse_event("content_block_start", {
        "type": "content_block_start",
        "index": 0,
        "content_block": {"type": "text", "text": ""}
    }))

    events.append(sse_event("content_block_delta", {
        "type": "content_block_delta",
        "index": 0,
        "delta": {"type": "text_delta", "text": "Let me read that file."}
    }))

    events.append(sse_event("content_block_stop", {
        "type": "content_block_stop",
        "index": 0
    }))

    # Tool use block
    events.append(sse_event("content_block_start", {
        "type": "content_block_start",
        "index": 1,
        "content_block": {
            "type": "tool_use",
            "id": "toolu_mock_01",
            "name": "read_file",
            "input": {}
        }
    }))

    events.append(sse_event("content_block_delta", {
        "type": "content_block_delta",
        "index": 1,
        "delta": {
            "type": "input_json_delta",
            "partial_json": '{"path": '
        }
    }))

    events.append(sse_event("content_block_delta", {
        "type": "content_block_delta",
        "index": 1,
        "delta": {
            "type": "input_json_delta",
            "partial_json": '"/etc/hostname"}'
        }
    }))

    events.append(sse_event("content_block_stop", {
        "type": "content_block_stop",
        "index": 1
    }))

    events.append(sse_event("message_delta", {
        "type": "message_delta",
        "delta": {"stop_reason": "tool_use", "stop_sequence": None},
        "usage": {"output_tokens": 30}
    }))

    events.append(sse_event("message_stop", {"type": "message_stop"}))

    return events


def error_response_stream():
    """Generate SSE events for an error response."""
    events = []

    events.append(sse_event("message_start", {
        "type": "message_start",
        "message": {
            "id": "msg_mock_err",
            "type": "message",
            "role": "assistant",
            "content": [],
            "model": "mock-claude",
            "stop_reason": None,
            "usage": {"input_tokens": 5, "output_tokens": 0}
        }
    }))

    events.append(sse_event("error", {
        "type": "error",
        "error": {
            "type": "overloaded_error",
            "message": "Overloaded"
        }
    }))

    return events


class MockClaudeHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        sys.stderr.write("[mock-claude] %s\n" % (format % args))

    def do_GET(self):
        if self.path == "/healthz":
            data = json.dumps({"status": "ok"}).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length) if content_length > 0 else b""

        if self.path != "/v1/messages":
            self.send_response(404)
            data = json.dumps({"error": "not found"}).encode("utf-8")
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return

        # Parse request
        try:
            request = json.loads(body) if body else {}
        except json.JSONDecodeError:
            request = {}

        # Check for stream flag
        is_stream = request.get("stream", False)

        # Determine scenario
        messages = request.get("messages", [])
        last_msg = messages[-1]["content"] if messages else ""
        has_tools = "tools" in request

        if last_msg == "error_test":
            events = error_response_stream()
        elif has_tools:
            events = tool_use_response_stream()
        else:
            events = text_response_stream()

        if is_stream:
            # SSE streaming response
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.end_headers()

            for event in events:
                self.wfile.write(event.encode("utf-8"))
                self.wfile.flush()
                time.sleep(0.05)  # Simulate streaming delay
        else:
            # Non-streaming response
            if "error" in last_msg:
                resp = {
                    "type": "error",
                    "error": {"type": "overloaded_error", "message": "Overloaded"}
                }
            else:
                resp = {
                    "id": "msg_mock_001",
                    "type": "message",
                    "role": "assistant",
                    "content": [{"type": "text", "text": "Hello from mock Claude!"}],
                    "model": "mock-claude",
                    "stop_reason": "end_turn",
                    "stop_sequence": None,
                    "usage": {"input_tokens": 10, "output_tokens": 5}
                }
            data = json.dumps(resp).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)


def ensure_certs():
    """Generate self-signed certs if they don't exist."""
    cert_dir = os.path.join(os.path.dirname(__file__), "certs")
    cert_file = os.path.join(cert_dir, "cert.pem")
    key_file = os.path.join(cert_dir, "key.pem")

    if os.path.exists(cert_file) and os.path.exists(key_file):
        return cert_file, key_file

    os.makedirs(cert_dir, exist_ok=True)
    os.system(
        f'openssl req -new -newkey rsa:2048 -x509 -sha256 -days 365 -nodes '
        f'-out {cert_file} -keyout {key_file} '
        f'-subj "/CN=localhost" 2>/dev/null'
    )
    sys.stderr.write(f"[mock-claude] Generated certs in {cert_dir}\n")
    return cert_file, key_file


def main():
    port = 4443
    use_tls = True

    for arg in sys.argv[1:]:
        if arg == "--no-tls":
            use_tls = False
        elif arg.isdigit():
            port = int(arg)

    server = HTTPServer(("0.0.0.0", port), MockClaudeHandler)

    if use_tls:
        cert_file, key_file = ensure_certs()
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert_file, key_file)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        sys.stderr.write(f"[mock-claude] TLS+SSE server on 0.0.0.0:{port}\n")
    else:
        sys.stderr.write(f"[mock-claude] Plain SSE server on 0.0.0.0:{port}\n")

    sys.stderr.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    server.server_close()
    sys.stderr.write("[mock-claude] Stopped\n")


if __name__ == "__main__":
    main()
