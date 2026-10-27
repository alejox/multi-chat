"""Chat de Twitch vía IRC sobre WebSocket (anónimo, solo lectura)."""
from __future__ import annotations

import json
import random
import socket
import time

from .. import net
from ..ws import WebSocket
from .base import Connector, emote, make_message, text

IRC_URL = "wss://irc-ws.chat.twitch.tv:443"
# Client-ID público del sitio web de Twitch, usado para consultar si el canal está en vivo.
GQL_CLIENT_ID = "kimne78kx3ncx6brgo4mv6wki5h1ko"
EMOTE_URL = "https://static-cdn.jtvnw.net/emoticons/v2/{id}/default/dark/2.0"

_TAG_ESCAPES = {"s": " ", ":": ";", "\\": "\\", "r": "\r", "n": "\n"}


def _unescape_tag(v: str) -> str:
    out, i = [], 0
    while i < len(v):
        c = v[i]
        if c == "\\" and i + 1 < len(v):
            out.append(_TAG_ESCAPES.get(v[i + 1], v[i + 1]))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def parse_irc(line: str) -> dict:
    tags: dict = {}
    prefix = ""
    if line.startswith("@"):
        raw, line = line[1:].split(" ", 1)
        for kv in raw.split(";"):
            k, _, v = kv.partition("=")
            tags[k] = _unescape_tag(v)
    if line.startswith(":"):
        prefix, line = line[1:].split(" ", 1)
    trailing = None
    if " :" in line:
        line, trailing = line.split(" :", 1)
    params = line.split()
    command = params.pop(0) if params else ""
    if trailing is not None:
        params.append(trailing)
    return {"tags": tags, "prefix": prefix, "command": command, "params": params}


def split_emotes(body: str, emotes_tag: str) -> list:
    """Convierte el tag `emotes` (id:ini-fin,ini-fin/id:...) en partes texto/emote."""
    ranges = []
    if emotes_tag:
        for spec in emotes_tag.split("/"):
            eid, _, positions = spec.partition(":")
            for pos in positions.split(","):
                a, _, b = pos.partition("-")
                if a.isdigit() and b.isdigit():
                    ranges.append((int(a), int(b), eid))
    ranges.sort()
    parts, cur = [], 0
    for a, b, eid in ranges:
        if a < cur or b >= len(body):
            continue
        if a > cur:
            parts.append(text(body[cur:a]))
        parts.append(emote(EMOTE_URL.format(id=eid), body[a:b + 1]))
        cur = b + 1
    if cur < len(body):
        parts.append(text(body[cur:]))
    return parts


def _badges(tag: str) -> list:
    names = []
    for b in tag.split(","):
        name = b.split("/", 1)[0]
        if name in ("broadcaster", "moderator", "vip", "subscriber", "founder", "partner", "staff"):
            names.append({"broadcaster": "owner", "founder": "subscriber", "partner": "verified"}.get(name, name))
    return names


def irc_to_message(msg: dict, channel: str) -> dict | None:
    tags, cmd = msg["tags"], msg["command"]
    author = tags.get("display-name") or msg["prefix"].split("!", 1)[0]
    if cmd == "PRIVMSG":
        body = msg["params"][-1] if msg["params"] else ""
        action = body.startswith("\x01ACTION ") and body.endswith("\x01")
        if action:
            body = body[8:-1]
        bits = tags.get("bits")
        m = make_message(
            "twitch", channel, author, split_emotes(body, tags.get("emotes", "")),
            color=tags.get("color") or None, badges=_badges(tags.get("badges", "")),
            kind="highlight" if bits else "chat", amount=f"{bits} bits" if bits else None,
            msg_id=tags.get("id"),
        )
        if action:
            m["action"] = True
        return m
    if cmd == "USERNOTICE":
        body = msg["params"][1] if len(msg["params"]) > 1 else ""
        system = tags.get("system-msg", "")
        return make_message(
            "twitch", channel, author, split_emotes(body, tags.get("emotes", "")) if body else [],
            color=tags.get("color") or None, badges=_badges(tags.get("badges", "")),
            kind="highlight", system=system, msg_id=tags.get("id"),
        )
    return None


def check_live(channel: str) -> bool | None:
    query = 'query{user(login:%s){stream{id}}}' % json.dumps(channel.lower())
    data = net.post_json("https://gql.twitch.tv/gql", {"query": query},
                         headers={"Client-Id": GQL_CLIENT_ID})
    user = (data.get("data") or {}).get("user")
    if user is None:
        return None
    return user.get("stream") is not None


class TwitchConnector(Connector):
    platform = "twitch"
    ws: WebSocket | None = None

    def close(self) -> None:
        if self.ws:
            self.ws.close()

    def session(self) -> None:
        chan = self.source.channel.lower()
        self.ws = ws = WebSocket(IRC_URL)
        ws.send_text("CAP REQ :twitch.tv/tags twitch.tv/commands")
        ws.send_text("PASS SCHMOOPIIE")
        ws.send_text(f"NICK justinfan{random.randint(10000, 99999)}")
        ws.send_text(f"JOIN #{chan}")
        ws.settimeout(1.0)
        last = time.time()
        while not self.stopped:
            try:
                data = ws.recv()
            except socket.timeout:
                if time.time() - last > 400:  # Twitch hace PING cada ~5 min
                    raise RuntimeError("sin respuesta del servidor")
                continue
            last = time.time()
            for line in data.split("\r\n"):
                if not line:
                    continue
                msg = parse_irc(line)
                cmd = msg["command"]
                if cmd == "PING":
                    ws.send_text("PONG :" + (msg["params"][-1] if msg["params"] else "tmi.twitch.tv"))
                elif cmd in ("ROOMSTATE", "366"):
                    self.report("connected", "")
                elif cmd == "RECONNECT":
                    raise RuntimeError("Twitch pidió reconectar")
                elif cmd == "NOTICE" and msg["tags"].get("msg-id", "").startswith("msg_channel_suspended"):
                    raise RuntimeError("canal suspendido o inexistente")
                else:
                    m = irc_to_message(msg, chan)
                    if m:
                        self.post(m)
