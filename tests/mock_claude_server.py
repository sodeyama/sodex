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

Agent Integration Scenarios (Plan 18):
  - "test_immediate": immediate text completion (no tools)
  - "test_one_tool": one read_file tool call, then text completion
  - "test_two_tools": list_dir -> read_file -> text completion
  - "test_max_steps": always returns tool_use (get_system_info)

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


def text_response_stream(text="Hello from mock Claude!", msg_id="msg_mock_001"):
    """Generate SSE events for a simple text response."""
    events = []

    events.append(sse_event("message_start", {
        "type": "message_start",
        "message": {
            "id": msg_id,
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

    # Send text in chunks (split into words)
    words = text.split(" ")
    for i, word in enumerate(words):
        prefix = " " if i > 0 else ""
        events.append(sse_event("content_block_delta", {
            "type": "content_block_delta",
            "index": 0,
            "delta": {"type": "text_delta", "text": prefix + word}
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


def tool_use_response_stream(tool_name="read_file", tool_id="toolu_mock_01",
                              tool_input_json='{"path": "/etc/hostname"}',
                              text_before="Let me use that tool.",
                              msg_id="msg_mock_tool"):
    """Generate SSE events for a tool_use response."""
    events = []

    events.append(sse_event("message_start", {
        "type": "message_start",
        "message": {
            "id": msg_id,
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
        "delta": {"type": "text_delta", "text": text_before}
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
            "id": tool_id,
            "name": tool_name,
            "input": {}
        }
    }))

    events.append(sse_event("content_block_delta", {
        "type": "content_block_delta",
        "index": 1,
        "delta": {
            "type": "input_json_delta",
            "partial_json": tool_input_json
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


# ---------------------------------------------------------------------------
# Agent Integration Scenario helpers (Plan 18)
# ---------------------------------------------------------------------------

def _count_tool_results(messages):
    """Count the number of tool_result blocks across all user messages."""
    count = 0
    for msg in messages:
        content = msg.get("content", "")
        if isinstance(content, list):
            for block in content:
                if isinstance(block, dict) and block.get("type") == "tool_result":
                    count += 1
    return count


def _find_scenario_keyword(messages):
    """Search all messages for a known scenario keyword.

    Returns the keyword string if found, else None.
    """
    keywords = ("test_immediate", "test_one_tool", "test_two_tools",
                "test_max_steps", "test_perm_blocked", "test_fetch_url_weather",
                "test_current_weather_requires_tool",
                "test_current_weather_retry_after_text_only",
                "test_session_resume_b", "test_session_resume_a",
                "test_repl_turn2", "test_repl_turn1")
    for msg in reversed(messages):
        content = msg.get("content", "")
        # content may be a plain string or a list of blocks
        if isinstance(content, str):
            for kw in keywords:
                if kw in content:
                    return kw
        elif isinstance(content, list):
            for block in reversed(content):
                if isinstance(block, dict):
                    text = block.get("text", "")
                    if isinstance(text, str):
                        for kw in keywords:
                            if kw in text:
                                return kw
                elif isinstance(block, str):
                    for kw in keywords:
                        if kw in block:
                            return kw
    return None


def _messages_contain_text(messages, needle):
    for msg in messages:
        content = msg.get("content", "")
        if isinstance(content, str):
            if needle in content:
                return True
        elif isinstance(content, list):
            for block in content:
                if isinstance(block, dict):
                    if needle in block.get("text", ""):
                        return True
                    if needle in block.get("content", ""):
                        return True
    return False


def _agent_scenario_events(scenario, tool_results_count, messages):
    """Return SSE events for a given agent integration scenario."""

    if scenario == "test_immediate":
        # Always return text with end_turn
        return text_response_stream(
            text="Immediate completion done.",
            msg_id="msg_integ_imm")

    if scenario == "test_one_tool":
        if tool_results_count == 0:
            # First turn: request read_file
            return tool_use_response_stream(
                tool_name="read_file",
                tool_id="toolu_integ_01",
                tool_input_json='{"path": "/etc/hostname"}',
                text_before="Reading file.",
                msg_id="msg_integ_1t_a")
        else:
            # After tool result: complete
            return text_response_stream(
                text="File content received",
                msg_id="msg_integ_1t_b")

    if scenario == "test_two_tools":
        if tool_results_count == 0:
            return tool_use_response_stream(
                tool_name="list_dir",
                tool_id="toolu_integ_2a",
                tool_input_json='{"path": "/"}',
                text_before="Listing directory.",
                msg_id="msg_integ_2t_a")
        elif tool_results_count == 1:
            return tool_use_response_stream(
                tool_name="read_file",
                tool_id="toolu_integ_2b",
                tool_input_json='{"path": "/etc/hostname"}',
                text_before="Now reading file.",
                msg_id="msg_integ_2t_b")
        else:
            return text_response_stream(
                text="Done",
                msg_id="msg_integ_2t_c")

    if scenario == "test_max_steps":
        # Always return a tool_use so the agent never ends naturally
        return tool_use_response_stream(
            tool_name="get_system_info",
            tool_id=f"toolu_integ_ms_{tool_results_count}",
            tool_input_json='{}',
            text_before="Getting info.",
            msg_id=f"msg_integ_ms_{tool_results_count}")

    if scenario == "test_perm_blocked":
        # Step 1: Try to write to /boot/ (will be blocked by permissions)
        # Step 2: After getting error, try /tmp/ (will succeed)
        # Step 3: Complete
        has_perm_error = _messages_contain_text(messages, "permission denied")
        if tool_results_count == 0 and not has_perm_error:
            # First attempt: write to protected path
            return tool_use_response_stream(
                tool_name="write_file",
                tool_id="toolu_integ_perm_1",
                tool_input_json='{"path": "/boot/blocked.txt", "content": "test"}',
                text_before="Writing to boot.",
                msg_id="msg_integ_perm_1")
        elif has_perm_error and tool_results_count <= 1:
            # After permission error: try alternative path
            return tool_use_response_stream(
                tool_name="write_file",
                tool_id="toolu_integ_perm_2",
                tool_input_json='{"path": "/tmp/allowed.txt", "content": "test"}',
                text_before="Trying alternative path.",
                msg_id="msg_integ_perm_2")
        else:
            return text_response_stream(
                text="Permission recovery succeeded.",
                msg_id="msg_integ_perm_done")

    if scenario == "test_fetch_url_weather":
        if tool_results_count == 0:
            return tool_use_response_stream(
                tool_name="fetch_url",
                tool_id="toolu_integ_fetch_weather",
                tool_input_json='{"url":"http://127.0.0.1:18081/weather/tokyo"}',
                text_before="Fetching weather source.",
                msg_id="msg_integ_fetch_weather_a")
        if (_messages_contain_text(messages, "Tokyo Weather 2026-03-19") and
                _messages_contain_text(messages, "http://127.0.0.1:18081/weather/tokyo")):
            return text_response_stream(
                text="Tokyo weather sourced from http://127.0.0.1:18081/weather/tokyo",
                msg_id="msg_integ_fetch_weather_b")
        return text_response_stream(
            text="fetch_url result missing weather data",
            msg_id="msg_integ_fetch_weather_missing")

    if scenario == "test_current_weather_requires_tool":
        if tool_results_count == 0:
            if _messages_contain_text(messages, "少なくとも1回は tool を使って確認してください"):
                return tool_use_response_stream(
                    tool_name="fetch_url",
                    tool_id="toolu_integ_current_weather",
                    tool_input_json='{"url":"http://127.0.0.1:18081/weather/tokyo"}',
                    text_before="Confirming current weather.",
                    msg_id="msg_integ_current_weather_a")
            return text_response_stream(
                text="東京はたぶん晴れです。",
                msg_id="msg_integ_current_weather_nohint")
        if (_messages_contain_text(messages, "Tokyo Weather 2026-03-19") and
                _messages_contain_text(messages, "http://127.0.0.1:18081/weather/tokyo")):
            return text_response_stream(
                text="Current weather verified with source http://127.0.0.1:18081/weather/tokyo",
                msg_id="msg_integ_current_weather_b")
        return text_response_stream(
            text="current weather verification missing source",
            msg_id="msg_integ_current_weather_missing")

    if scenario == "test_current_weather_retry_after_text_only":
        if tool_results_count == 0:
            if _messages_contain_text(messages, "前の応答は調査方針の説明だけで"):
                return tool_use_response_stream(
                    tool_name="fetch_url",
                    tool_id="toolu_integ_current_weather_retry",
                    tool_input_json='{"url":"http://127.0.0.1:18081/weather/tokyo"}',
                    text_before="Confirming current weather after retry.",
                    msg_id="msg_integ_current_weather_retry_a")
            return text_response_stream(
                text="東京の天気を調べます。まずwebsearchで最新の天気情報を検索します。",
                msg_id="msg_integ_current_weather_retry_plan")
        if (_messages_contain_text(messages, "Tokyo Weather 2026-03-19") and
                _messages_contain_text(messages, "http://127.0.0.1:18081/weather/tokyo")):
            return text_response_stream(
                text="Current weather verified after retry with source http://127.0.0.1:18081/weather/tokyo",
                msg_id="msg_integ_current_weather_retry_b")
        return text_response_stream(
            text="current weather retry missing source",
            msg_id="msg_integ_current_weather_retry_missing")

    if scenario == "test_repl_turn1":
        return text_response_stream(
            text="Turn one stored.",
            msg_id="msg_integ_repl_1")

    if scenario == "test_repl_turn2":
        if not _messages_contain_text(messages, "test_repl_turn1"):
            return text_response_stream(
                text="Turn two missing turn1 context.",
                msg_id="msg_integ_repl_2_missing")
        return text_response_stream(
            text="Turn two remembers turn1.",
            msg_id="msg_integ_repl_2")

    if scenario == "test_session_resume_a":
        return text_response_stream(
            text="Session turn A stored.",
            msg_id="msg_integ_resume_a")

    if scenario == "test_session_resume_b":
        if not _messages_contain_text(messages, "test_session_resume_a"):
            return text_response_stream(
                text="Resume missing prior session.",
                msg_id="msg_integ_resume_b_missing")
        return text_response_stream(
            text="Resumed session remembered.",
            msg_id="msg_integ_resume_b")

    # Fallback (should not happen)
    return text_response_stream(text="Unknown scenario", msg_id="msg_integ_unk")


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
        has_tools = "tools" in request

        # --- Agent integration scenarios (Plan 18) ---
        scenario = _find_scenario_keyword(messages)
        if scenario is not None:
            tool_results_count = _count_tool_results(messages)
            sys.stderr.write(
                f"[mock-claude] agent-integ scenario={scenario} "
                f"tool_results={tool_results_count}\n")
            events = _agent_scenario_events(scenario, tool_results_count, messages)
        else:
            # --- Legacy scenario detection ---
            last_msg = messages[-1]["content"] if messages else ""
            if isinstance(last_msg, list):
                # Extract text from content blocks
                last_msg = " ".join(
                    b.get("text", "") for b in last_msg
                    if isinstance(b, dict) and b.get("type") == "text"
                )

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
            if isinstance(last_msg, str) and "error" in last_msg:
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
