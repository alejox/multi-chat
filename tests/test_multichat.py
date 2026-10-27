import json
import os
import sys
import threading
import time
import unittest
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from multichat.app import App  # noqa: E402
from multichat.hub import Hub  # noqa: E402
from multichat.manager import MODE_ALWAYS, MODE_AUTO, Manager, Source, parse_entry  # noqa: E402
from multichat.platforms import kick, twitch, youtube  # noqa: E402


class ParseEntryTest(unittest.TestCase):
    def check(self, entry, platform, channel):
        s = parse_entry(entry)
        self.assertEqual((s.platform, s.channel), (platform, channel), entry)
        return s

    def test_prefixes(self):
        self.check("twitch:MiCanal", "twitch", "micanal")
        self.check("kick:Canal", "kick", "canal")
        self.check("youtube:@Canal", "youtube", "@Canal")
        self.check("youtube:Canal", "youtube", "@Canal")
        self.check("yt:UCabcdefghijklmnopqrstuv", "youtube", "UCabcdefghijklmnopqrstuv")
        self.assertEqual(parse_entry("kick:canal#1234").extra, "1234")

    def test_urls(self):
        self.check("https://www.twitch.tv/MiCanal", "twitch", "micanal")
        self.check("https://kick.com/canal", "kick", "canal")
        self.check("https://www.youtube.com/@Canal", "youtube", "@Canal")
        s = self.check("https://www.youtube.com/watch?v=dQw4w9WgXcQ", "youtube", "dQw4w9WgXcQ")
        self.assertTrue(s.video_fixed)
        self.check("https://youtu.be/dQw4w9WgXcQ", "youtube", "dQw4w9WgXcQ")
        self.check("youtube.com/live/dQw4w9WgXcQ", "youtube", "dQw4w9WgXcQ")

    def test_errors(self):
        for bad in ("canal", "facebook:x", "twitch:"):
            with self.assertRaises(ValueError):
                parse_entry(bad)


class TwitchTest(unittest.TestCase):
    LINE = ("@badge-info=subscriber/8;badges=broadcaster/1,subscriber/6;color=#0D4200;"
            "display-name=Mi\\sCanal;emotes=25:0-4,11-15;id=abc-1 "
            ":micanal!micanal@micanal.tmi.twitch.tv PRIVMSG #micanal :Kappa hola Kappa")

    def test_parse_irc(self):
        m = twitch.parse_irc(self.LINE)
        self.assertEqual(m["command"], "PRIVMSG")
        self.assertEqual(m["params"], ["#micanal", "Kappa hola Kappa"])
        self.assertEqual(m["tags"]["display-name"], "Mi Canal")

    def test_message(self):
        msg = twitch.irc_to_message(twitch.parse_irc(self.LINE), "micanal")
        self.assertEqual(msg["author"], "Mi Canal")
        self.assertEqual(msg["badges"], ["owner", "subscriber"])
        self.assertEqual([p["type"] for p in msg["parts"]], ["emote", "text", "emote"])
        self.assertEqual(msg["parts"][1]["text"], " hola ")
        self.assertEqual(msg["id"], "abc-1")

    def test_action_and_unicode_emotes(self):
        line = "@emotes=25:2-6 :u!u@u PRIVMSG #c :\x01ACTION 😀 Kappa\x01"
        msg = twitch.irc_to_message(twitch.parse_irc(line), "c")
        self.assertTrue(msg["action"])
        self.assertEqual(msg["parts"][1], {"type": "emote", "url": twitch.EMOTE_URL.format(id=25), "name": "Kappa"})

    def test_usernotice(self):
        line = ("@display-name=Fan;msg-id=resub;system-msg=Fan\\ssubscribed\\sfor\\s3\\smonths "
                ":tmi.twitch.tv USERNOTICE #c :gracias!")
        msg = twitch.irc_to_message(twitch.parse_irc(line), "c")
        self.assertEqual(msg["kind"], "highlight")
        self.assertEqual(msg["system"], "Fan subscribed for 3 months")


class KickTest(unittest.TestCase):
    def test_content(self):
        parts = kick.split_content("hola [emote:37226:KEKW] que tal")
        self.assertEqual(parts[1], {"type": "emote", "url": kick.EMOTE_URL.format(id="37226"), "name": "KEKW"})
        self.assertEqual(parts[2]["text"], " que tal")

    def test_event(self):
        data = {"id": "x1", "content": "hi", "sender": {"username": "pepe", "identity": {
            "color": "#FF0000", "badges": [{"type": "moderator"}, {"type": "og"}]}}}
        m = kick.event_to_message(data, "canal")
        self.assertEqual((m["author"], m["color"], m["badges"]), ("pepe", "#FF0000", ["moderator", "vip"]))


class YouTubeTest(unittest.TestCase):
    def test_action(self):
        action = {"addChatItemAction": {"item": {"liveChatPaidMessageRenderer": {
            "id": "yt1", "authorName": {"simpleText": "Ana"},
            "purchaseAmountText": {"simpleText": "$5.00"},
            "authorPhoto": {"thumbnails": [{"url": "https://yt/a.jpg"}]},
            "authorBadges": [{"liveChatAuthorBadgeRenderer": {"icon": {"iconType": "MODERATOR"}}}],
            "message": {"runs": [{"text": "hola "}, {"emoji": {"emojiId": "😀", "shortcuts": [":grin:"]}},
                                 {"emoji": {"emojiId": "UC/x", "isCustomEmoji": True, "shortcuts": [":yt:"],
                                            "image": {"thumbnails": [{"url": "https://yt/e.png"}]}}}]},
        }}}}
        m = youtube.action_to_message(action, "@canal")
        self.assertEqual((m["author"], m["amount"], m["kind"]), ("Ana", "$5.00", "highlight"))
        self.assertEqual(m["badges"], ["moderator"])
        self.assertEqual(m["parts"][1], {"type": "text", "text": "😀"})
        self.assertEqual(m["parts"][2]["type"], "emote")

    def test_continuation(self):
        lc = {"continuations": [{"timedContinuationData": {"continuation": "abc", "timeoutMs": 4000}}]}
        self.assertEqual(youtube.next_continuation(lc), ("abc", 4000))


class ManagerTest(unittest.TestCase):
    def setUp(self):
        self.hub = Hub()
        self.mgr = Manager(self.hub)

    def tearDown(self):
        self.mgr.stop()

    def test_auto_mode_wants(self):
        self.mgr.mode = MODE_AUTO
        s = Source("twitch", "x")
        s.live = None
        self.assertFalse(self.mgr._wants(s))
        self.mgr.streaming = True
        self.assertTrue(self.mgr._wants(s))
        s.live = False
        self.assertFalse(self.mgr._wants(s))
        s.live = True
        self.mgr.streaming = False
        self.assertTrue(self.mgr._wants(s))
        self.mgr.mode = MODE_ALWAYS
        s.live = False
        self.assertTrue(self.mgr._wants(s))

    def test_multistream_state(self):
        a, b = Source("twitch", "a"), Source("kick", "b")
        a.live, b.live = True, True
        self.mgr.sources = {a.key: a, b.key: b}
        st = self.mgr._state()
        self.assertTrue(st["multistream"])
        self.assertEqual(st["live_platforms"], ["twitch", "kick"])
        b.live = False
        self.assertFalse(self.mgr._state()["multistream"])
        self.mgr.streaming, self.mgr.service = True, "Restream.io"
        self.assertTrue(self.mgr._state()["multistream"])


class ServerTest(unittest.TestCase):
    def test_demo_end_to_end(self):
        app = App()
        app.apply(["demo:demo", "facebook:nope"], MODE_ALWAYS, 0)
        try:
            base = f"http://127.0.0.1:{app.port}"
            for page in ("/dock", "/overlay", "/static/common.js"):
                with urllib.request.urlopen(base + page, timeout=5) as r:
                    self.assertEqual(r.status, 200)
            got = []

            def read_sse():
                with urllib.request.urlopen(base + "/events", timeout=10) as r:
                    for raw in r:
                        line = raw.decode().strip()
                        if line.startswith("event: message"):
                            got.append(line)
                            return

            t = threading.Thread(target=read_sse, daemon=True)
            t.start()
            t.join(10)
            self.assertTrue(got, "no llegó ningún mensaje por SSE")
            with urllib.request.urlopen(base + "/api/state", timeout=5) as r:
                state = json.loads(r.read())["state"]
            self.assertEqual(state["sources"][0]["status"], "connected")
            self.assertEqual(len(state["errors"]), 1)
            self.assertTrue(all(m["source"] == "demo:demo" for m in app.hub.history()))
        finally:
            app.stop()
        time.sleep(0.1)


if __name__ == "__main__":
    unittest.main()
