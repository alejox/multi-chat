"""Fuente de demostración: genera mensajes falsos para probar el dock y el overlay."""
from __future__ import annotations

import random

from .base import Connector, emote, make_message, text

USERS = [
    ("twitch", "LunaGamer", "#FF7F50", ["subscriber"]),
    ("twitch", "pixel_pancho", "#1E90FF", ["moderator"]),
    ("twitch", "NachoVIP", "#DAA520", ["vip"]),
    ("youtube", "María Fernández", None, ["member"]),
    ("youtube", "Carlos R.", None, []),
    ("youtube", "Canal Oficial", None, ["owner"]),
    ("kick", "kicker_mx", "#53FC18", []),
    ("kick", "zeta_arg", "#FF69B4", ["subscriber"]),
]
LINES = [
    "¡Holaaa desde Buenos Aires!", "qué buen stream hoy", "jajaja", "GG", "ese clutch estuvo increíble",
    "¿a qué hora termina el directo?", "saludos a todos los del chat de Kick", "primera vez por aquí 👋",
    "LUL", "eso fue épico", "¿qué teclado usas?", "vamos que se puede 💪",
]
KAPPA = "https://static-cdn.jtvnw.net/emoticons/v2/25/default/dark/2.0"


class DemoConnector(Connector):
    platform = "demo"

    def session(self) -> None:
        self.report("connected", "")
        while not self.wait(random.uniform(0.4, 1.8)):
            platform, name, color, badges = random.choice(USERS)
            parts = [text(random.choice(LINES))]
            if platform == "twitch" and random.random() < 0.3:
                parts += [text(" "), emote(KAPPA, "Kappa")]
            kind, amount, system = "chat", None, None
            r = random.random()
            if r < 0.05:
                kind, amount = "highlight", random.choice(["$5.00", "$20.00", "500 bits"])
            elif r < 0.08:
                kind, system, parts = "highlight", f"{name} se suscribió por 6 meses", []
            self.post(make_message(platform, "demo", name, parts, color=color, badges=badges,
                                   kind=kind, amount=amount, system=system))
