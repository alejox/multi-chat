"""Servidor HTTP local: sirve el dock, el overlay y el flujo de eventos (SSE)."""
from __future__ import annotations

import json
import mimetypes
import os
import queue
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from .hub import Hub

WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")
PAGES = {"/": "dock.html", "/dock": "dock.html", "/overlay": "overlay.html"}


def make_handler(hub: Hub, on_refresh=None):
    class Handler(BaseHTTPRequestHandler):
        server_version = "Multichat"

        def log_message(self, fmt, *args):  # silencio: OBS no necesita el log de cada petición
            pass

        def _send(self, code: int, body: bytes, ctype: str) -> None:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, data, code: int = 200) -> None:
            self._send(code, json.dumps(data).encode(), "application/json; charset=utf-8")

        def do_GET(self):  # noqa: N802
            u = urlparse(self.path)
            path = u.path.rstrip("/") or "/"
            if path in PAGES:
                return self._file(PAGES[path])
            if path.startswith("/static/"):
                return self._file(path[len("/static/"):])
            if path == "/api/state":
                return self._json({"state": hub.state(), "history": hub.history()})
            if path == "/events":
                return self._events(parse_qs(u.query))
            self._send(404, b"not found", "text/plain")

        def do_POST(self):  # noqa: N802
            path = urlparse(self.path).path.rstrip("/")
            if path == "/api/clear":
                hub.clear()
                return self._json({"ok": True})
            if path == "/api/refresh":
                if on_refresh:
                    on_refresh()
                return self._json({"ok": True})
            self._send(404, b"not found", "text/plain")

        def _file(self, name: str) -> None:
            full = os.path.normpath(os.path.join(WEB_DIR, name))
            if not full.startswith(WEB_DIR + os.sep) or not os.path.isfile(full):
                return self._send(404, b"not found", "text/plain")
            ctype = mimetypes.guess_type(full)[0] or "application/octet-stream"
            if ctype.startswith("text/") or ctype.endswith("javascript"):
                ctype += "; charset=utf-8"
            with open(full, "rb") as f:
                self._send(200, f.read(), ctype)

        def _events(self, qs: dict) -> None:
            q = hub.subscribe()
            try:
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Connection", "keep-alive")
                self.end_headers()
                self._event("state", hub.state())
                if qs.get("history", ["1"])[0] != "0":
                    self._event("history", hub.history())
                while not hub.closed:
                    try:
                        item = q.get(timeout=15)
                    except queue.Empty:
                        self.wfile.write(b": ping\n\n")
                        self.wfile.flush()
                        continue
                    if item is None:
                        break
                    self._event(*item)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
                pass
            finally:
                hub.unsubscribe(q)
                self.close_connection = True

        def _event(self, kind: str, data) -> None:
            payload = json.dumps(data, ensure_ascii=False)
            self.wfile.write(f"event: {kind}\ndata: {payload}\n\n".encode("utf-8"))
            self.wfile.flush()

    return Handler


class Server:
    def __init__(self, hub: Hub, host: str, port: int, on_refresh=None):
        self.httpd = ThreadingHTTPServer((host, port), make_handler(hub, on_refresh))
        self.httpd.daemon_threads = True
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True, name="multichat-http")
        self.thread.start()

    @property
    def port(self) -> int:
        return self.httpd.server_address[1]

    def stop(self) -> None:
        self.httpd.shutdown()
        self.httpd.server_close()
