"""Clase base para los conectores de chat de cada plataforma."""
from __future__ import annotations

import logging
import threading
import time
import uuid
from typing import Callable

log = logging.getLogger("multichat")


def text(t: str) -> dict:
    return {"type": "text", "text": t}


def emote(url: str, name: str) -> dict:
    return {"type": "emote", "url": url, "name": name}


def make_message(platform: str, channel: str, author: str, parts: list, *, color: str | None = None,
                 badges: list | None = None, kind: str = "chat", amount: str | None = None,
                 avatar: str | None = None, msg_id: str | None = None, system: str | None = None) -> dict:
    return {
        "id": msg_id or uuid.uuid4().hex,
        "platform": platform,
        "channel": channel,
        "author": author,
        "color": color,
        "badges": badges or [],
        "parts": parts,
        "kind": kind,          # chat | highlight (super chat, sub, bits, raid...)
        "amount": amount,
        "avatar": avatar,
        "system": system,      # texto de sistema ("se suscribió 12 meses")
        "ts": time.time(),
    }


class Connector(threading.Thread):
    """Hilo que mantiene la conexión a un chat y reconecta con backoff."""

    platform = ""

    def __init__(self, source, post: Callable[[dict], None], report: Callable[[str, str], None]):
        super().__init__(daemon=True, name=f"multichat-{source.key}")
        self.source = source
        self.post = post
        self.report = report
        self._stop_evt = threading.Event()

    @property
    def stopped(self) -> bool:
        return self._stop_evt.is_set()

    def wait(self, seconds: float) -> bool:
        """Espera interrumpible. Devuelve True si se pidió parar."""
        return self._stop_evt.wait(seconds)

    def stop(self) -> None:
        self._stop_evt.set()
        try:
            self.close()
        except Exception:  # noqa: BLE001
            pass

    def close(self) -> None:
        """Cierra recursos de red para desbloquear `session()`."""

    def session(self) -> None:
        raise NotImplementedError

    def run(self) -> None:
        backoff = 2.0
        while not self.stopped:
            started = time.time()
            try:
                self.report("connecting", "")
                self.session()
            except Exception as e:  # noqa: BLE001
                if self.stopped:
                    break
                log.warning("[%s] %s", self.source.key, e)
                self.report("error", str(e)[:200])
            finally:
                try:
                    self.close()
                except Exception:  # noqa: BLE001
                    pass
            if self.stopped:
                break
            if time.time() - started > 60:
                backoff = 2.0
            if self.wait(backoff):
                break
            backoff = min(backoff * 2, 60.0)
        self.report("idle", "")
