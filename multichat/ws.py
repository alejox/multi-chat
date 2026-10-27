"""Cliente WebSocket mínimo (RFC 6455) usando solo la librería estándar.

OBS trae su propio intérprete de Python sin paquetes extra, así que evitamos
dependencias externas como `websocket-client`.
"""
from __future__ import annotations

import base64
import os
import socket
import ssl
import struct
from urllib.parse import urlparse

USER_AGENT = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/130.0 Safari/537.36"
)

OP_CONT, OP_TEXT, OP_BINARY, OP_CLOSE, OP_PING, OP_PONG = 0x0, 0x1, 0x2, 0x8, 0x9, 0xA


class WebSocketClosed(Exception):
    pass


class WebSocket:
    def __init__(self, url: str, timeout: float = 15.0, origin: str | None = None):
        u = urlparse(url)
        secure = u.scheme == "wss"
        host = u.hostname or ""
        port = u.port or (443 if secure else 80)
        path = (u.path or "/") + ("?" + u.query if u.query else "")

        sock = socket.create_connection((host, port), timeout=timeout)
        if secure:
            ctx = ssl.create_default_context()
            sock = ctx.wrap_socket(sock, server_hostname=host)
        self.sock = sock
        self._buf = b""
        self._frags: list[bytes] = []
        self._frag_op = OP_TEXT
        self.closed = False

        key = base64.b64encode(os.urandom(16)).decode()
        lines = [
            f"GET {path} HTTP/1.1",
            f"Host: {host}" + (f":{port}" if u.port else ""),
            "Upgrade: websocket",
            "Connection: Upgrade",
            f"Sec-WebSocket-Key: {key}",
            "Sec-WebSocket-Version: 13",
            f"User-Agent: {USER_AGENT}",
        ]
        if origin:
            lines.append(f"Origin: {origin}")
        self.sock.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())

        while b"\r\n\r\n" not in self._buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise WebSocketClosed("handshake interrumpido")
            self._buf += chunk
            if len(self._buf) > 65536:
                raise WebSocketClosed("handshake demasiado grande")
        head, self._buf = self._buf.split(b"\r\n\r\n", 1)
        status = head.split(b"\r\n", 1)[0].decode(errors="replace")
        if " 101 " not in status + " ":
            raise WebSocketClosed(f"handshake rechazado: {status}")

    def settimeout(self, seconds: float | None) -> None:
        self.sock.settimeout(seconds)

    # -- envío -------------------------------------------------------------
    def _send_frame(self, opcode: int, payload: bytes) -> None:
        header = bytearray([0x80 | opcode])
        n = len(payload)
        if n < 126:
            header.append(0x80 | n)
        elif n < 65536:
            header.append(0x80 | 126)
            header += struct.pack("!H", n)
        else:
            header.append(0x80 | 127)
            header += struct.pack("!Q", n)
        mask = os.urandom(4)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(bytes(header) + mask + masked)

    def send_text(self, text: str) -> None:
        self._send_frame(OP_TEXT, text.encode("utf-8"))

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        try:
            self._send_frame(OP_CLOSE, struct.pack("!H", 1000))
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass

    # -- recepción ---------------------------------------------------------
    def _fill(self, n: int) -> None:
        """Asegura `n` bytes en el buffer sin consumirlos.

        Si salta un timeout a mitad de un frame, los datos ya leídos se
        conservan y la siguiente llamada continúa donde se quedó.
        """
        while len(self._buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise WebSocketClosed("conexión cerrada por el servidor")
            self._buf += chunk

    def _read_frame(self) -> tuple[bool, int, bytes]:
        self._fill(2)
        b0, b1 = self._buf[0], self._buf[1]
        fin, opcode = bool(b0 & 0x80), b0 & 0x0F
        masked, n = bool(b1 & 0x80), b1 & 0x7F
        offset = 2
        if n == 126:
            self._fill(4)
            n = struct.unpack("!H", self._buf[2:4])[0]
            offset = 4
        elif n == 127:
            self._fill(10)
            n = struct.unpack("!Q", self._buf[2:10])[0]
            offset = 10
        mask = b""
        if masked:
            self._fill(offset + 4)
            mask = self._buf[offset:offset + 4]
            offset += 4
        self._fill(offset + n)
        payload = self._buf[offset:offset + n]
        self._buf = self._buf[offset + n:]
        if masked:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        return fin, opcode, payload

    def recv(self) -> str:
        """Devuelve el siguiente mensaje de texto. Propaga `socket.timeout`."""
        while True:
            fin, opcode, payload = self._read_frame()
            if opcode == OP_PING:
                self._send_frame(OP_PONG, payload)
                continue
            if opcode == OP_PONG:
                continue
            if opcode == OP_CLOSE:
                self.close()
                raise WebSocketClosed("el servidor cerró la conexión")
            if opcode in (OP_TEXT, OP_BINARY):
                self._frags = [payload]
                self._frag_op = opcode
            elif opcode == OP_CONT:
                self._frags.append(payload)
            if fin:
                data = b"".join(self._frags)
                self._frags = []
                return data.decode("utf-8", errors="replace")
