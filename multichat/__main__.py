"""Modo standalone (sin OBS): python -m multichat twitch:canal youtube:@canal kick:canal"""
from __future__ import annotations

import argparse
import json
import logging
import time

from .app import DEFAULT_PORT, App
from .manager import MODE_ALWAYS, MODE_AUTO


def main() -> None:
    p = argparse.ArgumentParser(prog="multichat", description="Multichat para OBS (modo standalone)")
    p.add_argument("chats", nargs="*", help="twitch:canal youtube:@canal kick:canal (o URLs)")
    p.add_argument("--config", help="archivo JSON con {\"chats\": [...], \"mode\": ..., \"port\": ...}")
    p.add_argument("--port", type=int)
    p.add_argument("--mode", choices=[MODE_AUTO, MODE_ALWAYS],
                   help="auto: solo chats de plataformas en vivo · always: todos (por defecto)")
    p.add_argument("--demo", action="store_true", help="añade mensajes de prueba")
    args = p.parse_args()

    cfg: dict = {}
    if args.config:
        with open(args.config, encoding="utf-8") as f:
            cfg = json.load(f)
    chats = list(cfg.get("chats", [])) + args.chats
    if args.demo:
        chats.append("demo:demo")
    mode = args.mode or cfg.get("mode") or MODE_ALWAYS
    port = args.port or cfg.get("port") or DEFAULT_PORT

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
    app = App()
    app.apply(chats, mode, port)
    if app.server_error:
        raise SystemExit(app.server_error)
    print(f"Dock:    {app.url('dock')}")
    print(f"Overlay: {app.url('overlay')}")
    print("Ctrl+C para salir.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        app.stop()


if __name__ == "__main__":
    main()
