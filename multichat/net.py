"""Peticiones HTTP sencillas con urllib (sin dependencias)."""
from __future__ import annotations

import json
import urllib.request

from .ws import USER_AGENT

DEFAULT_HEADERS = {
    "User-Agent": USER_AGENT,
    "Accept-Language": "es-ES,es;q=0.9,en;q=0.8",
}


def request(url: str, data=None, headers: dict | None = None, timeout: float = 10.0) -> bytes:
    h = dict(DEFAULT_HEADERS)
    if headers:
        h.update(headers)
    body = None
    if data is not None:
        body = json.dumps(data).encode("utf-8") if not isinstance(data, bytes) else data
        h.setdefault("Content-Type", "application/json")
    req = urllib.request.Request(url, data=body, headers=h)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def get_text(url: str, headers: dict | None = None, timeout: float = 10.0) -> str:
    return request(url, headers=headers, timeout=timeout).decode("utf-8", errors="replace")


def get_json(url: str, headers: dict | None = None, timeout: float = 10.0):
    return json.loads(request(url, headers=headers, timeout=timeout))


def post_json(url: str, data, headers: dict | None = None, timeout: float = 10.0):
    return json.loads(request(url, data=data, headers=headers, timeout=timeout))
