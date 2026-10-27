"""Chat de Kick vía su WebSocket público de Pusher."""
from __future__ import annotations

import json
import re
import socket
import time

from .. import net
from ..ws import WebSocket
from .base import Connector, emote, make_message, text

PUSHER_URL = ("wss://ws-us2.pusher.com/app/32cbd69e4b950bf97679"
              "?protocol=7&client=js&version=8.4.0&flash=false")
EMOTE_URL = "https://files.kick.com/emotes/{id}/fullsize"
_EMOTE_RE = re.compile(r"\[emote:(\d+):([^\]]*)\]")


def channel_info(slug: str) -> dict:
    last_err: Exception | None = None
    for url in (f"https://kick.com/api/v2/channels/{slug}", f"https://kick.com/api/v1/channels/{slug}"):
        try:
            return net.get_json(url, headers={"Accept": "application/json"})
        except Exception as e:  # noqa: BLE001
            last_err = e
    raise RuntimeError(f"no se pudo consultar el canal de Kick ({last_err})")


def check_live(slug: str) -> bool | None:
    try:
        info = channel_info(slug)
    except RuntimeError:
        return None  # Kick bloquea a veces la API (Cloudflare): estado desconocido
    return bool(info.get("livestream"))


def split_content(content: str) -> list:
    parts, cur = [], 0
    for m in _EMOTE_RE.finditer(content):
        if m.start() > cur:
            parts.append(text(content[cur:m.start()]))
        parts.append(emote(EMOTE_URL.format(id=m.group(1)), m.group(2)))
        cur = m.end()
    if cur < len(content):
        parts.append(text(content[cur:]))
    return parts


_BADGE_MAP = {"broadcaster": "owner", "moderator": "moderator", "vip": "vip", "og": "vip",
              "subscriber": "subscriber", "founder": "subscriber", "verified": "verified",
              "staff": "staff"}


def event_to_message(data: dict, channel: str) -> dict:
    sender = data.get("sender") or {}
    identity = sender.get("identity") or {}
    badges = []
    for b in identity.get("badges") or []:
        name = _BADGE_MAP.get(b.get("type", ""))
        if name and name not in badges:
            badges.append(name)
    return make_message(
        "kick", channel, sender.get("username", "?"), split_content(data.get("content", "")),
        color=identity.get("color") or None, badges=badges, msg_id=str(data.get("id") or "") or None,
    )


class KickConnector(Connector):
    platform = "kick"
    ws: WebSocket | None = None

    def close(self) -> None:
        if self.ws:
            self.ws.close()

    def session(self) -> None:
        slug = self.source.channel
        chatroom = self.source.extra
        if not chatroom:
            info = channel_info(slug)
            chatroom = str((info.get("chatroom") or {}).get("id") or "")
            if not chatroom:
                raise RuntimeError("canal de Kick sin chatroom")
            self.source.extra = chatroom  # se reutiliza en reconexiones

        self.ws = ws = WebSocket(PUSHER_URL, origin="https://kick.com")
        ws.send_text(json.dumps({"event": "pusher:subscribe",
                                 "data": {"auth": "", "channel": f"chatrooms.{chatroom}.v2"}}))
        ws.settimeout(1.0)
        last = time.time()
        while not self.stopped:
            try:
                raw = ws.recv()
            except socket.timeout:
                if time.time() - last > 120:
                    ws.send_text(json.dumps({"event": "pusher:ping", "data": {}}))
                    last = time.time() - 60
                continue
            last = time.time()
            evt = json.loads(raw)
            name = evt.get("event", "")
            if name == "pusher:ping":
                ws.send_text(json.dumps({"event": "pusher:pong", "data": {}}))
            elif name == "pusher_internal:subscription_succeeded":
                self.report("connected", "")
            elif name == "pusher:error":
                raise RuntimeError(f"Pusher: {evt.get('data')}")
            elif name == "App\\Events\\ChatMessageEvent":
                data = evt.get("data")
                if isinstance(data, str):
                    data = json.loads(data)
                self.post(event_to_message(data, slug))
