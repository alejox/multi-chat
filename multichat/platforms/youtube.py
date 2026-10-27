"""Chat de YouTube leyendo el endpoint interno del chat en vivo (sin API key)."""
from __future__ import annotations

import re

from .. import net
from .base import Connector, emote, make_message, text

COOKIES = {"Cookie": "CONSENT=YES+1; SOCS=CAI"}  # evita la página de consentimiento en la UE
_CANON_RE = re.compile(r'<link rel="canonical" href="https://www\.youtube\.com/watch\?v=([\w-]{11})"')
_VIDEO_RE = re.compile(r"(?:v=|youtu\.be/|/live/|/shorts/)([\w-]{11})")


def channel_url(channel: str) -> str:
    if channel.startswith("UC") and len(channel) == 24:
        return f"https://www.youtube.com/channel/{channel}"
    return f"https://www.youtube.com/{channel if channel.startswith('@') else '@' + channel}"


def find_live_video(channel: str) -> str | None:
    """Devuelve el ID del directo activo de un canal, o None si no está en vivo."""
    html = net.get_text(channel_url(channel) + "/live", headers=COOKIES)
    m = _CANON_RE.search(html)
    if m and '"isLiveNow":true' in html:
        return m.group(1)
    return None


def video_is_live(video_id: str) -> bool:
    html = net.get_text(f"https://www.youtube.com/watch?v={video_id}", headers=COOKIES)
    return '"isLiveNow":true' in html


def check_live(source) -> bool | None:
    if source.video_fixed:
        return video_is_live(source.extra)
    vid = find_live_video(source.channel)
    source.extra = vid
    return vid is not None


def _runs_to_parts(runs: list) -> list:
    parts = []
    for r in runs or []:
        if "text" in r:
            parts.append(text(r["text"]))
        elif "emoji" in r:
            e = r["emoji"]
            thumbs = (e.get("image") or {}).get("thumbnails") or []
            shortcut = (e.get("shortcuts") or [e.get("emojiId", "")])[0]
            if e.get("isCustomEmoji") and thumbs:
                parts.append(emote(thumbs[-1]["url"], shortcut))
            else:
                parts.append(text(e.get("emojiId") or shortcut))
    return parts


def _simple(obj) -> str:
    if not obj:
        return ""
    if "simpleText" in obj:
        return obj["simpleText"]
    return "".join(r.get("text", "") for r in obj.get("runs", []))


def _badges(renderer: dict) -> list:
    out = []
    for b in renderer.get("authorBadges") or []:
        br = b.get("liveChatAuthorBadgeRenderer") or {}
        icon = (br.get("icon") or {}).get("iconType", "")
        if icon == "OWNER":
            out.append("owner")
        elif icon == "MODERATOR":
            out.append("moderator")
        elif icon in ("VERIFIED", "CHECK_CIRCLE_THICK"):
            out.append("verified")
        elif br.get("customThumbnail"):
            out.append("member")
    return out


def action_to_message(action: dict, channel: str) -> dict | None:
    item = (action.get("addChatItemAction") or {}).get("item") or {}
    for key, kind in (("liveChatTextMessageRenderer", "chat"),
                      ("liveChatPaidMessageRenderer", "highlight"),
                      ("liveChatMembershipItemRenderer", "highlight"),
                      ("liveChatPaidStickerRenderer", "highlight")):
        r = item.get(key)
        if not r:
            continue
        photos = (r.get("authorPhoto") or {}).get("thumbnails") or []
        system = _simple(r.get("headerSubtext")) if key == "liveChatMembershipItemRenderer" else None
        return make_message(
            "youtube", channel, _simple(r.get("authorName")) or "?",
            _runs_to_parts((r.get("message") or {}).get("runs")),
            badges=_badges(r), kind=kind,
            amount=_simple(r.get("purchaseAmountText")) or None,
            avatar=photos[0]["url"] if photos else None,
            msg_id=r.get("id"), system=system,
        )
    return None


def next_continuation(lc: dict) -> tuple[str | None, int]:
    for c in lc.get("continuations") or []:
        for k in ("invalidationContinuationData", "timedContinuationData", "reloadContinuationData"):
            if k in c:
                return c[k].get("continuation"), int(c[k].get("timeoutMs", 3000))
    return None, 3000


class YouTubeConnector(Connector):
    platform = "youtube"

    def session(self) -> None:
        src = self.source
        while not self.stopped:
            video = src.extra if src.video_fixed else (src.extra or find_live_video(src.channel))
            if video:
                break
            self.report("waiting", "esperando a que empiece el directo")
            if self.wait(30):
                return
        else:
            return

        html = net.get_text(f"https://www.youtube.com/live_chat?is_popout=1&v={video}", headers=COOKIES)
        key = re.search(r'"INNERTUBE_API_KEY":"([^"]+)"', html)
        ver = re.search(r'"INNERTUBE_CONTEXT_CLIENT_VERSION":"([^"]+)"', html)
        cont = re.search(r'"continuation":"([^"]+)"', html)
        if not cont:
            if not src.video_fixed:
                src.extra = None
            raise RuntimeError("el directo no tiene chat disponible")
        url = "https://www.youtube.com/youtubei/v1/live_chat/get_live_chat"
        if key:
            url += "?key=" + key.group(1)
        context = {"client": {"clientName": "WEB", "clientVersion": ver.group(1) if ver else "2.20241001.00.00"}}
        continuation: str | None = cont.group(1)

        self.report("connected", "")
        first = True
        while not self.stopped and continuation:
            data = net.post_json(url, {"context": context, "continuation": continuation}, headers=COOKIES)
            lc = (data.get("continuationContents") or {}).get("liveChatContinuation")
            if not lc:
                break
            if not first:  # el primer lote es historial antiguo: no lo repetimos
                for action in lc.get("actions") or []:
                    m = action_to_message(action, src.channel)
                    if m:
                        self.post(m)
            first = False
            continuation, timeout_ms = next_continuation(lc)
            if self.wait(min(max(timeout_ms / 1000, 1.0), 6.0)):
                return
        if not src.video_fixed:
            src.extra = None  # el directo terminó; buscar uno nuevo
        raise RuntimeError("el chat del directo terminó")


def parse_video(value: str) -> str | None:
    m = _VIDEO_RE.search(value)
    return m.group(1) if m else None
