#!/usr/bin/env python3
"""host 側の構造化 Web fetch gateway。"""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen
import base64
import hashlib
import json
import os
import shlex
import subprocess
import sys

from web_fetch_extract import (
    ExtractedDocument,
    extract_html_document,
    extract_json_document,
    extract_text_document,
)


DEFAULT_BIND = "0.0.0.0"
DEFAULT_PORT = 8081
DEFAULT_TIMEOUT_MS = 5000
DEFAULT_MAX_BYTES = 262144
DEFAULT_MAX_CHARS = 4000
DEFAULT_MAX_CHARS_LIMIT = 16000
DEFAULT_MAX_LINKS = 8
DEFAULT_ALLOWED_TYPES = ["text/html", "text/plain", "application/json"]


@dataclass
class GatewayConfig:
    bind: str = DEFAULT_BIND
    port: int = DEFAULT_PORT
    allowlist: list[str] = field(default_factory=list)
    timeout_ms: int = DEFAULT_TIMEOUT_MS
    max_bytes: int = DEFAULT_MAX_BYTES
    default_max_chars: int = DEFAULT_MAX_CHARS
    max_chars_limit: int = DEFAULT_MAX_CHARS_LIMIT
    max_links: int = DEFAULT_MAX_LINKS
    allowed_content_types: list[str] = field(default_factory=lambda: list(DEFAULT_ALLOWED_TYPES))
    render_js_command: str = ""

    @classmethod
    def from_env(cls) -> "GatewayConfig":
        config = cls()
        config_path = os.environ.get("SODEX_WEBFETCH_CONFIG")
        if config_path:
            config.apply_mapping(_load_json_config(Path(config_path)))

        config.bind = os.environ.get("SODEX_WEBFETCH_BIND", config.bind)
        config.port = _env_int("SODEX_WEBFETCH_PORT", config.port)
        config.allowlist = _env_csv("SODEX_WEBFETCH_ALLOWLIST", config.allowlist)
        config.timeout_ms = _env_int("SODEX_WEBFETCH_TIMEOUT_MS", config.timeout_ms)
        config.max_bytes = _env_int("SODEX_WEBFETCH_MAX_BYTES", config.max_bytes)
        config.default_max_chars = _env_int(
            "SODEX_WEBFETCH_DEFAULT_MAX_CHARS", config.default_max_chars
        )
        config.max_chars_limit = _env_int(
            "SODEX_WEBFETCH_MAX_CHARS_LIMIT", config.max_chars_limit
        )
        config.max_links = _env_int("SODEX_WEBFETCH_MAX_LINKS", config.max_links)
        config.allowed_content_types = _env_csv(
            "SODEX_WEBFETCH_ALLOWED_TYPES", config.allowed_content_types
        )
        config.render_js_command = os.environ.get(
            "SODEX_WEBFETCH_RENDER_JS_COMMAND", config.render_js_command
        )
        return config

    def apply_mapping(self, mapping: dict[str, Any]) -> None:
        if not mapping:
            return
        self.bind = str(mapping.get("bind", self.bind))
        self.port = int(mapping.get("port", self.port))
        self.allowlist = _normalize_list(mapping.get("allowlist", self.allowlist))
        self.timeout_ms = int(mapping.get("timeout_ms", self.timeout_ms))
        self.max_bytes = int(mapping.get("max_bytes", self.max_bytes))
        self.default_max_chars = int(
            mapping.get("default_max_chars", self.default_max_chars)
        )
        self.max_chars_limit = int(mapping.get("max_chars_limit", self.max_chars_limit))
        self.max_links = int(mapping.get("max_links", self.max_links))
        self.allowed_content_types = _normalize_list(
            mapping.get("allowed_content_types", self.allowed_content_types)
        )
        self.render_js_command = str(
            mapping.get("render_js_command", self.render_js_command)
        )


@dataclass
class FetchedResource:
    url: str
    final_url: str
    status: int
    content_type: str
    body: bytes
    truncated_by_bytes: bool


def _env_int(name: str, default: int) -> int:
    raw = os.environ.get(name)
    if raw is None or not raw.strip():
        return default
    try:
        return int(raw)
    except ValueError:
        return default


def _normalize_list(value: Any) -> list[str]:
    if isinstance(value, list):
        return [str(item).strip() for item in value if str(item).strip()]
    if isinstance(value, str):
        return [item.strip() for item in value.split(",") if item.strip()]
    return []


def _env_csv(name: str, default: list[str]) -> list[str]:
    raw = os.environ.get(name)
    if raw is None:
        return list(default)
    return _normalize_list(raw)


def _load_json_config(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def _error_body(code: str, message: str, *, url: str = "") -> dict[str, Any]:
    body: dict[str, Any] = {
        "error": message,
        "code": code,
    }
    if url:
        body["url"] = url
    return body


def is_url_allowed(url: str, config: GatewayConfig) -> bool:
    parsed = urlparse(url)
    if parsed.scheme not in {"http", "https"}:
        return False
    if not parsed.hostname:
        return False
    if not config.allowlist:
        return False

    host = parsed.hostname.lower()
    host_port = f"{host}:{parsed.port}" if parsed.port else host
    for rule in config.allowlist:
        item = rule.lower()
        if item == host or item == host_port:
            return True
        if item.startswith(".") and host.endswith(item):
            return True
    return False


def _fetch_static_url(
    *,
    url: str,
    method: str,
    timeout_ms: int,
    max_bytes: int,
) -> FetchedResource:
    headers = {
        "User-Agent": "sodex-webfetch/1.0",
        "Accept": "text/html, text/plain, application/json",
    }
    request = Request(url=url, method=method, headers=headers)
    timeout = max(timeout_ms, 1) / 1000.0

    try:
        with urlopen(request, timeout=timeout) as response:
            body = b"" if method == "HEAD" else response.read(max_bytes + 1)
            return FetchedResource(
                url=url,
                final_url=response.geturl(),
                status=response.getcode(),
                content_type=response.headers.get("Content-Type", ""),
                body=body[:max_bytes],
                truncated_by_bytes=len(body) > max_bytes,
            )
    except HTTPError as exc:
        body = b"" if method == "HEAD" else exc.read(max_bytes + 1)
        return FetchedResource(
            url=url,
            final_url=exc.geturl() or url,
            status=exc.code,
            content_type=exc.headers.get("Content-Type", ""),
            body=body[:max_bytes],
            truncated_by_bytes=len(body) > max_bytes,
        )


def _fetch_via_render_js(
    *,
    url: str,
    method: str,
    timeout_ms: int,
    max_bytes: int,
    max_chars: int,
    config: GatewayConfig,
) -> tuple[int, FetchedResource | dict[str, Any]]:
    if not config.render_js_command:
        return 400, _error_body("render_js_disabled", "render_js backend is disabled", url=url)

    payload = {
        "url": url,
        "method": method,
        "timeout_ms": timeout_ms,
        "max_bytes": max_bytes,
        "max_chars": max_chars,
    }
    try:
        proc = subprocess.run(
            shlex.split(config.render_js_command),
            input=json.dumps(payload),
            text=True,
            capture_output=True,
            timeout=max(timeout_ms, 1) / 1000.0,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return 502, _error_body("render_js_failed", str(exc), url=url)

    if proc.returncode != 0:
        message = proc.stderr.strip() or "render_js backend exited with error"
        return 502, _error_body("render_js_failed", message, url=url)

    try:
        result = json.loads(proc.stdout)
    except json.JSONDecodeError:
        return 502, _error_body("render_js_invalid", "render_js backend returned invalid JSON", url=url)

    body_text = result.get("body")
    body_base64 = result.get("body_base64")
    if isinstance(body_base64, str):
        try:
            body = base64.b64decode(body_base64)
        except ValueError:
            return 502, _error_body("render_js_invalid", "body_base64 decode failed", url=url)
    elif isinstance(body_text, str):
        body = body_text.encode("utf-8", "replace")
    else:
        body = b""

    return 200, FetchedResource(
        url=url,
        final_url=str(result.get("final_url") or url),
        status=int(result.get("status", 200)),
        content_type=str(result.get("content_type") or "text/html"),
        body=body[:max_bytes],
        truncated_by_bytes=len(body) > max_bytes,
    )


def _extract_resource(
    resource: FetchedResource,
    *,
    max_chars: int,
    max_links: int,
) -> ExtractedDocument:
    primary_type = resource.content_type.split(";", 1)[0].strip().lower()
    body_text = resource.body.decode("utf-8", "replace")

    if resource.status == 204 or not resource.body:
        return ExtractedDocument(title="", excerpt="", main_text="", links=[], truncated=False)
    if primary_type == "text/html":
        return extract_html_document(
            body_text,
            base_url=resource.final_url,
            max_chars=max_chars,
            max_links=max_links,
        )
    if primary_type == "application/json":
        return extract_json_document(body_text, max_chars=max_chars)
    return extract_text_document(body_text, max_chars=max_chars)


def handle_fetch_request(
    payload: dict[str, Any],
    config: GatewayConfig,
) -> tuple[int, dict[str, Any]]:
    url = str(payload.get("url") or "").strip()
    method = str(payload.get("method") or "GET").upper()
    render_js = bool(payload.get("render_js", False))
    max_bytes = min(int(payload.get("max_bytes", config.max_bytes)), config.max_bytes)
    max_chars = min(
        int(payload.get("max_chars", config.default_max_chars)),
        config.max_chars_limit,
    )

    if not url:
        return 400, _error_body("missing_url", "url is required")
    if method not in {"GET", "HEAD"}:
        return 400, _error_body("method_not_allowed", "only GET and HEAD are supported", url=url)
    if max_bytes <= 0 or max_chars <= 0:
        return 400, _error_body("invalid_limits", "max_bytes and max_chars must be positive", url=url)
    if not is_url_allowed(url, config):
        return 403, _error_body("allowlist_denied", "target URL is not allowed", url=url)

    try:
        if render_js:
            status_code, js_result = _fetch_via_render_js(
                url=url,
                method=method,
                timeout_ms=config.timeout_ms,
                max_bytes=max_bytes,
                max_chars=max_chars,
                config=config,
            )
            if status_code != 200 or not isinstance(js_result, FetchedResource):
                return status_code, js_result
            resource = js_result
        else:
            resource = _fetch_static_url(
                url=url,
                method=method,
                timeout_ms=config.timeout_ms,
                max_bytes=max_bytes,
            )
    except URLError as exc:
        reason = getattr(exc, "reason", exc)
        return 504, _error_body("fetch_failed", str(reason), url=url)
    except TimeoutError as exc:
        return 504, _error_body("fetch_timeout", str(exc), url=url)

    primary_type = resource.content_type.split(";", 1)[0].strip().lower()
    if primary_type and primary_type not in config.allowed_content_types:
        return 415, _error_body(
            "unsupported_content_type",
            f"unsupported content type: {primary_type}",
            url=url,
        )

    extracted = _extract_resource(
        resource,
        max_chars=max_chars,
        max_links=config.max_links,
    )
    fetched_at = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    return 200, {
        "url": url,
        "final_url": resource.final_url,
        "status": resource.status,
        "content_type": primary_type or resource.content_type,
        "title": extracted.title,
        "excerpt": extracted.excerpt,
        "main_text": extracted.main_text,
        "links": extracted.links,
        "fetched_at": fetched_at,
        "source_hash": f"sha256:{hashlib.sha256(resource.body).hexdigest()}",
        "truncated": bool(resource.truncated_by_bytes or extracted.truncated),
        "method": method,
        "render_js": render_js,
    }


class WebFetchHandler(BaseHTTPRequestHandler):
    server_version = "SodexWebFetch/1.0"

    @property
    def config(self) -> GatewayConfig:
        return self.server.config  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args: Any) -> None:
        sys.stderr.write("[webfetch-gateway] " + (fmt % args) + "\n")

    def do_GET(self) -> None:
        if self.path != "/healthz":
            self._send_json(404, {"error": "not found"})
            return
        self._send_json(200, {"status": "ok"})

    def do_POST(self) -> None:
        if self.path != "/fetch":
            self._send_json(404, {"error": "not found"})
            return

        content_length = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(content_length) if content_length > 0 else b""
        try:
            payload = json.loads(body.decode("utf-8")) if body else {}
        except json.JSONDecodeError:
            self._send_json(400, _error_body("invalid_json", "request body must be JSON"))
            return

        status, result = handle_fetch_request(payload, self.config)
        self._send_json(status, result)

    def _send_json(self, status: int, body: dict[str, Any]) -> None:
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def run_server(config: GatewayConfig) -> None:
    server = HTTPServer((config.bind, config.port), WebFetchHandler)
    server.config = config  # type: ignore[attr-defined]
    sys.stderr.write(
        f"[webfetch-gateway] listening on {config.bind}:{config.port} "
        f"allowlist={','.join(config.allowlist) or '(deny-all)'}\n"
    )
    sys.stderr.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


def main(argv: list[str] | None = None) -> int:
    args = list(argv or sys.argv[1:])
    config = GatewayConfig.from_env()

    if args and args[0] not in {"-h", "--help"}:
        config.port = int(args.pop(0))
    if args and args[0] in {"-h", "--help"}:
        print("usage: web_fetch_gateway.py [port]")
        return 0

    run_server(config)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
