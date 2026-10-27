"""Bus de eventos: guarda historial y reparte mensajes a los clientes SSE."""
from __future__ import annotations

import queue
import threading
from collections import deque


class Hub:
    def __init__(self, history: int = 200):
        self._lock = threading.Lock()
        self._history: deque = deque(maxlen=history)
        self._subs: set[queue.Queue] = set()
        self._state: dict = {}
        self.closed = False

    def subscribe(self) -> queue.Queue:
        q: queue.Queue = queue.Queue(maxsize=2000)
        with self._lock:
            self._subs.add(q)
        return q

    def unsubscribe(self, q: queue.Queue) -> None:
        with self._lock:
            self._subs.discard(q)

    def history(self) -> list:
        with self._lock:
            return list(self._history)

    def state(self) -> dict:
        with self._lock:
            return dict(self._state)

    def _publish(self, kind: str, data) -> None:
        for q in list(self._subs):
            try:
                q.put_nowait((kind, data))
            except queue.Full:
                pass  # cliente demasiado lento: descartamos

    def post_message(self, msg: dict) -> None:
        with self._lock:
            self._history.append(msg)
            self._publish("message", msg)

    def set_state(self, state: dict) -> None:
        with self._lock:
            self._state = state
            self._publish("state", state)

    def clear(self) -> None:
        with self._lock:
            self._history.clear()
            self._publish("clear", {})

    def close(self) -> None:
        with self._lock:
            self.closed = True
            for q in self._subs:
                try:
                    q.put_nowait(None)
                except queue.Full:
                    pass
