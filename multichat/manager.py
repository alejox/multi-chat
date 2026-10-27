"""Gestiona las fuentes de chat: detecta qué plataformas están en vivo y
conecta/desconecta los chats según el modo elegido."""
from __future__ import annotations

import logging
import re
import threading
import time
from concurrent.futures import ThreadPoolExecutor

from . import platforms
from .hub import Hub
from .platforms import youtube

log = logging.getLogger("multichat")

PLATFORM_NAMES = {"twitch": "Twitch", "youtube": "YouTube", "kick": "Kick", "demo": "Demo"}
# Palabras del servicio de OBS que indican que se transmite a varias plataformas a la vez.
MULTI_SERVICES = ("restream", "castr", "streamyard", "multistream", "prism")

MODE_AUTO = "auto"      # conectar solo los chats de las plataformas que estén en vivo
MODE_ALWAYS = "always"  # conectar siempre todos los chats configurados


class Source:
    def __init__(self, platform: str, channel: str, extra: str | None = None, video_fixed: bool = False):
        self.platform = platform
        self.channel = channel
        self.extra = extra              # Kick: id del chatroom · YouTube: id del vídeo en directo
        self.video_fixed = video_fixed  # YouTube: el usuario indicó un vídeo concreto
        self.key = f"{platform}:{channel}".lower()
        self.live: bool | None = None
        self.status = "idle"            # idle | connecting | waiting | connected | error
        self.detail = ""
        self.connector = None
        self.next_check = 0.0
        self.checking = False

    def to_dict(self) -> dict:
        return {"key": self.key, "platform": self.platform, "channel": self.channel,
                "live": self.live, "status": self.status, "detail": self.detail}


def parse_entry(entry: str) -> Source:
    """Interpreta 'twitch:canal', 'youtube:@handle', 'kick:canal' o una URL del canal."""
    raw = entry.strip()
    if not raw:
        raise ValueError("entrada vacía")
    url = re.sub(r"^https?://(www\.|m\.)?", "", raw, flags=re.I)
    if re.match(r"^twitch\.tv/", url, re.I):
        return Source("twitch", url.split("/")[1].lower())
    if re.match(r"^kick\.com/", url, re.I):
        return Source("kick", url.split("/")[1].lower())
    if re.match(r"^(youtube\.com|youtu\.be)/", url, re.I):
        vid = youtube.parse_video(url)
        if vid:
            return Source("youtube", vid, extra=vid, video_fixed=True)
        seg = url.split("/")[1:]
        if seg and seg[0] == "channel" and len(seg) > 1:
            return Source("youtube", seg[1])
        if seg and seg[0].startswith("@"):
            return Source("youtube", seg[0])
        raise ValueError(f"URL de YouTube no reconocida: {raw}")

    platform, sep, channel = raw.partition(":")
    platform = {"yt": "youtube", "ttv": "twitch"}.get(platform.lower(), platform.lower())
    channel = channel.strip()
    if not sep or not channel:
        raise ValueError(f"usa el formato plataforma:canal (ej. twitch:micanal), recibido: {raw}")
    if platform == "twitch":
        return Source("twitch", channel.lstrip("#").lower())
    if platform == "kick":
        slug, _, chatroom = channel.partition("#")  # kick:canal#12345 fuerza el id del chatroom
        return Source("kick", slug.lower(), extra=chatroom or None)
    if platform == "youtube":
        vid = youtube.parse_video(channel)
        if vid:
            return Source("youtube", vid, extra=vid, video_fixed=True)
        if not (channel.startswith("UC") and len(channel) == 24) and not channel.startswith("@"):
            channel = "@" + channel
        return Source("youtube", channel)
    if platform == "demo":
        return Source("demo", channel or "demo")
    raise ValueError(f"plataforma no soportada: {platform} (usa twitch, youtube o kick)")


class Manager:
    def __init__(self, hub: Hub):
        self.hub = hub
        self.sources: dict[str, Source] = {}
        self.mode = MODE_AUTO
        self.streaming = False
        self.service = ""
        self.errors: list[str] = []
        self._lock = threading.RLock()
        self._wake = threading.Event()
        self._stop = threading.Event()
        self._boost_until = 0.0
        self._last_state = None
        self._pool = ThreadPoolExecutor(max_workers=4, thread_name_prefix="multichat-check")
        self._thread = threading.Thread(target=self._loop, daemon=True, name="multichat-manager")
        self._thread.start()

    # -- API pública --------------------------------------------------------
    def configure(self, entries: list[str], mode: str = MODE_AUTO) -> None:
        new: dict[str, Source] = {}
        errors = []
        for e in entries:
            if not e.strip():
                continue
            try:
                s = parse_entry(e)
            except ValueError as err:
                errors.append(str(err))
                continue
            new.setdefault(s.key, s)
        with self._lock:
            self.mode = mode if mode in (MODE_AUTO, MODE_ALWAYS) else MODE_AUTO
            self.errors = errors
            for key in list(self.sources):
                if key not in new:
                    self._disconnect(self.sources.pop(key))
            for key, s in new.items():
                if key not in self.sources:
                    self.sources[key] = s
        self._wake.set()

    def set_streaming(self, active: bool, service: str = "") -> None:
        with self._lock:
            self.streaming = active
            if service or not active:
                self.service = service
            if active:
                # Las plataformas tardan unos segundos en marcar el canal como en vivo:
                # durante 3 minutos comprobamos con más frecuencia.
                self._boost_until = time.time() + 180
                for s in self.sources.values():
                    s.next_check = 0
        self._wake.set()

    def refresh(self) -> None:
        with self._lock:
            for s in self.sources.values():
                s.next_check = 0
        self._wake.set()

    def stop(self) -> None:
        self._stop.set()
        self._wake.set()
        with self._lock:
            for s in self.sources.values():
                self._disconnect(s)
        self._pool.shutdown(wait=False)

    # -- interno ------------------------------------------------------------
    def _wants(self, s: Source) -> bool:
        if self.mode == MODE_ALWAYS:
            return True
        return s.live is True or (s.live is None and self.streaming)

    def _connect(self, s: Source) -> None:
        cls = platforms.CONNECTORS[s.platform]
        holder: list = []

        def report(status: str, detail: str) -> None:
            with self._lock:
                if holder and s.connector is holder[0]:
                    s.status, s.detail = status, detail
            self._wake.set()

        def post(msg: dict) -> None:
            msg["source"] = s.key
            self.hub.post_message(msg)

        conn = cls(s, post, report)
        holder.append(conn)
        s.connector = conn
        conn.start()

    def _disconnect(self, s: Source) -> None:
        if s.connector:
            s.connector.stop()
            s.connector = None
        s.status, s.detail = "idle", ""

    def _check(self, s: Source) -> None:
        try:
            live = platforms.check_live(s)
        except Exception as e:  # noqa: BLE001
            log.info("[%s] no se pudo comprobar si está en vivo: %s", s.key, e)
            live = None
        with self._lock:
            s.live = live
            s.checking = False
        self._wake.set()

    def _state(self) -> dict:
        live = [s.platform for s in self.sources.values() if s.live]
        live_platforms = sorted(set(live), key=list(PLATFORM_NAMES).index)
        svc = self.service.lower()
        return {
            "mode": self.mode,
            "streaming": self.streaming,
            "service": self.service,
            "multistream": len(live_platforms) > 1 or (self.streaming and any(w in svc for w in MULTI_SERVICES)),
            "live_platforms": live_platforms,
            "sources": [s.to_dict() for s in self.sources.values()],
            "errors": list(self.errors),
        }

    def _loop(self) -> None:
        while not self._stop.is_set():
            now = time.time()
            with self._lock:
                interval = 15 if now < self._boost_until else 60
                for s in list(self.sources.values()):
                    if not s.checking and now >= s.next_check:
                        s.checking = True
                        s.next_check = now + interval
                        try:
                            self._pool.submit(self._check, s)
                        except RuntimeError:
                            return  # pool cerrado
                    want = self._wants(s)
                    if want and s.connector is None:
                        self._connect(s)
                    elif not want and s.connector is not None:
                        self._disconnect(s)
                state = self._state()
            if state != self._last_state:
                self._last_state = state
                self.hub.set_state(state)
            self._wake.wait(5)
            self._wake.clear()
