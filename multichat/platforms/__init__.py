from . import demo, kick, twitch, youtube

CONNECTORS = {
    "twitch": twitch.TwitchConnector,
    "youtube": youtube.YouTubeConnector,
    "kick": kick.KickConnector,
    "demo": demo.DemoConnector,
}


def check_live(source):
    """True/False si se sabe si el canal está en vivo, None si no se puede saber."""
    if source.platform == "twitch":
        return twitch.check_live(source.channel)
    if source.platform == "kick":
        return kick.check_live(source.channel)
    if source.platform == "youtube":
        return youtube.check_live(source)
    if source.platform == "demo":
        return True
    return None
