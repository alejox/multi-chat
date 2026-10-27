"""Une hub, gestor y servidor. Lo usan tanto el script de OBS como el modo standalone."""
from __future__ import annotations

import logging

from .hub import Hub
from .manager import MODE_AUTO, Manager
from .server import Server

log = logging.getLogger("multichat")
DEFAULT_PORT = 8765


class App:
    def __init__(self, host: str = "127.0.0.1"):
        self.host = host
        self.hub = Hub()
        self.manager = Manager(self.hub)
        self.server: Server | None = None
        self.port = 0
        self.server_error = ""

    def apply(self, entries: list[str], mode: str = MODE_AUTO, port: int = DEFAULT_PORT) -> None:
        if self.server is None or port != self.port:
            if self.server:
                self.server.stop()
                self.server = None
            try:
                self.server = Server(self.hub, self.host, port, on_refresh=self.manager.refresh)
                self.port = self.server.port
                self.server_error = ""
                log.info("Multichat escuchando en http://%s:%d", self.host, self.port)
            except OSError as e:
                self.port = port
                self.server_error = f"No se pudo abrir el puerto {port}: {e}"
                log.error(self.server_error)
        self.manager.configure(entries, mode)

    def set_streaming(self, active: bool, service: str = "") -> None:
        self.manager.set_streaming(active, service)

    def url(self, page: str = "dock") -> str:
        return f"http://localhost:{self.port}/{page}"

    def stop(self) -> None:
        self.manager.stop()
        self.hub.close()
        if self.server:
            self.server.stop()
            self.server = None
