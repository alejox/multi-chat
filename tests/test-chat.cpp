// Pruebas de los parsers (sin red ni OBS). Se compilan en CI y con tests/CMakeLists.txt.

#include "../src/chat/message.hpp"
#include "../src/chat/platforms.hpp"
#include "../src/chat/updates.hpp"

#include <QJsonDocument>
#include <QtTest/QtTest>

using namespace chat;

// Datos de prueba en fixtures.cpp (moc no entiende los raw strings).
const char *fixture(int index);

class ChatTest : public QObject {
	Q_OBJECT

private:
	static SourceInfo entry(const QString &text)
	{
		SourceInfo s;
		QString error;
		if (!parseEntry(text, s, error))
			qWarning() << "parseEntry falló:" << text << error;
		return s;
	}

	// Trama binaria de Supabase Realtime construida según _decodeUserBroadcast del bundle de la
	// página (no es una captura real): tipo, longitudes de topic, evento y metadatos, codificación y
	// después esos campos seguidos del contenido.
	static QByteArray realtimeFrame(const QByteArray &topic, const QByteArray &event, const QByteArray &meta,
					const QByteArray &payload, char kind = 4, char encoding = 1)
	{
		QByteArray out;
		out.append(kind);
		out.append(char(topic.size()));
		out.append(char(event.size()));
		out.append(char(meta.size()));
		out.append(encoding);
		return out + topic + event + meta + payload;
	}

private slots:
	void starviosBroadcastFrame()
	{
		using starvios::Frame;
		const QByteArray payload = "{\"op\":\"INSERT\",\"msg\":{\"id\":\"m1\",\"body\":\"hóla\"}}";
		for (const QByteArray &meta : {QByteArray(), QByteArray("{\"id\":\"x\"}")}) {
			const Frame f = starvios::decodeFrame(realtimeFrame("realtime:stream:s1", "msg", meta, payload));
			QVERIFY(f.type == Frame::Type::Broadcast);
			QCOMPARE(f.topic, QString("realtime:stream:s1"));
			QCOMPARE(f.event, QString("msg"));
			QCOMPARE(f.payload["op"].toString(), QString("INSERT"));
			QCOMPARE(f.payload["msg"].toObject()["body"].toString(), QString("hóla"));
		}
		// Metadatos que no son JSON: la página los descartaría, aquí no invalidan el mensaje.
		QVERIFY(starvios::decodeFrame(realtimeFrame("t", "msg", "{", payload)).type == Frame::Type::Broadcast);
		// Bien formadas, pero sin contenido JSON o de otro tipo: se ignoran sin ser un error.
		QVERIFY(starvios::decodeFrame(realtimeFrame("t", "msg", "", "\x01\x02", 4, 0)).type == Frame::Type::Ignored);
		QVERIFY(starvios::decodeFrame(realtimeFrame("t", "msg", "", payload, 3)).type == Frame::Type::Ignored);
		QVERIFY(starvios::decodeFrame(realtimeFrame("t", "msg", "", payload, 9)).type == Frame::Type::Ignored);
	}

	void starviosMalformedFrames()
	{
		using starvios::Frame;
		QVERIFY(starvios::decodeFrame(QByteArray()).type == Frame::Type::Invalid);
		QVERIFY(starvios::decodeFrame(QByteArray("\x04", 1)).type == Frame::Type::Invalid);
		const QByteArray good = realtimeFrame("topic", "msg", "{}", "{\"op\":\"INSERT\"}");
		// Cortada en cualquier punto, nunca se lee fuera de los límites ni se da por buena.
		for (qsizetype cut = 0; cut < good.size(); ++cut)
			QVERIFY(starvios::decodeFrame(QByteArrayView(good).first(cut)).type == Frame::Type::Invalid);
		QVERIFY(starvios::decodeFrame(realtimeFrame("t", "msg", "", "no json")).type == Frame::Type::Invalid);
		QVERIFY(starvios::decodeFrame(realtimeFrame("t", "msg", "", "[1,2]")).type == Frame::Type::Invalid);
		QVERIFY(starvios::decodeFrame(QByteArray(32, char(0xFF))).type != Frame::Type::Broadcast);
		for (const char *text : {"[", "{}", "[1,2,3]", "\"hola\"", "[null,null,\"t\",\"e\"]"})
			QVERIFY(starvios::decodeFrame(QByteArray(text)).type == Frame::Type::Invalid);
	}

	void starviosControlFrame()
	{
		using starvios::Frame;
		const Frame reply = starvios::decodeFrame(QByteArray(
			"[\"1\",\"1\",\"realtime:stream:s1\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]"));
		QVERIFY(reply.type == Frame::Type::Control);
		QCOMPARE(reply.ref, QString("1"));
		QCOMPARE(reply.topic, QString("realtime:stream:s1"));
		QCOMPARE(reply.event, QString("phx_reply"));
		QCOMPARE(reply.payload["status"].toString(), QString("ok"));
		const Frame beat = starvios::decodeFrame(QByteArray("[null,\"7\",\"phoenix\",\"phx_reply\",{}]"));
		QVERIFY(beat.type == Frame::Type::Control);
		QCOMPARE(beat.ref, QString("7"));
		// ref numérico o nulo, y un contenido que no es un objeto: no rompen la trama.
		const Frame other = starvios::decodeFrame(QByteArray("[null,5,\"t\",\"system\",null]"));
		QVERIFY(other.type == Frame::Type::Control);
		QCOMPARE(other.ref, QString("5"));
		QVERIFY(other.payload.isEmpty());
	}

	void parseEntries()
	{
		QCOMPARE(entry("twitch:MiCanal").key, QString("twitch:micanal"));
		QCOMPARE(entry("kick:Canal").channel, QString("canal"));
		QCOMPARE(entry("kick:canal#1234").extra, QString("1234"));
		QCOMPARE(entry("youtube:@Canal").channel, QString("@Canal"));
		QCOMPARE(entry("youtube:Canal").channel, QString("@Canal"));
		QCOMPARE(entry("yt:UCabcdefghijklmnopqrstuv").channel, QString("UCabcdefghijklmnopqrstuv"));
		QCOMPARE(entry("https://www.twitch.tv/MiCanal").channel, QString("micanal"));
		QCOMPARE(entry("https://kick.com/canal").platform, QString("kick"));
		QCOMPARE(entry("https://www.youtube.com/@Canal").channel, QString("@Canal"));
		SourceInfo v = entry("https://www.youtube.com/watch?v=dQw4w9WgXcQ");
		QCOMPARE(v.channel, QString("dQw4w9WgXcQ"));
		QVERIFY(v.videoFixed);
		QCOMPARE(entry("https://youtu.be/dQw4w9WgXcQ").extra, QString("dQw4w9WgXcQ"));
		QCOMPARE(entry("youtube.com/live/dQw4w9WgXcQ").extra, QString("dQw4w9WgXcQ"));
		QCOMPARE(entry("tiktok:@Mi.Canal").key, QString("tiktok:@mi.canal"));
		QCOMPARE(entry("tiktok:mi_canal").channel, QString("@mi_canal"));
		QCOMPARE(entry("https://www.tiktok.com/@MiCanal/live").channel, QString("@micanal"));

		SourceInfo s;
		QString error;
		for (const char *bad : {"canal", "facebook:x", "twitch:", "", "tiktok:mal nombre", "tiktok:@"})
			QVERIFY2(!parseEntry(bad, s, error), bad);
	}

	void twitchMessage()
	{
		const QString line = "@badge-info=subscriber/8;badges=broadcaster/1,subscriber/6;color=#0D4200;"
				     "display-name=Mi\\sCanal;emotes=25:0-4,11-15;id=abc-1 "
				     ":micanal!micanal@micanal.tmi.twitch.tv PRIVMSG #micanal :Kappa hola Kappa";
		twitch::IrcMessage irc = twitch::parseIrc(line);
		QCOMPARE(irc.command, QString("PRIVMSG"));
		QCOMPARE(irc.params, QStringList({"#micanal", "Kappa hola Kappa"}));
		QJsonObject m = twitch::toMessage(irc, "micanal");
		QCOMPARE(m["author"].toString(), QString("Mi Canal"));
		QCOMPARE(m["badges"].toArray(), QJsonArray({"owner", "subscriber"}));
		QJsonArray parts = m["parts"].toArray();
		QCOMPARE(parts.size(), 3);
		QCOMPARE(parts[0].toObject()["type"].toString(), QString("emote"));
		QCOMPARE(parts[1].toObject()["text"].toString(), QString(" hola "));
		QCOMPARE(m["id"].toString(), QString("abc-1"));
	}

	void twitchUnicodeAndAction()
	{
		// El emoji ocupa 2 unidades UTF-16 pero 1 punto de código: Kappa empieza en 2.
		twitch::IrcMessage irc = twitch::parseIrc(QString::fromUtf8("@emotes=25:2-6 :u!u@u PRIVMSG #c :\x01"
									    "ACTION 😀 Kappa\x01"));
		QJsonObject m = twitch::toMessage(irc, "c");
		QVERIFY(m["action"].toBool());
		QJsonArray parts = m["parts"].toArray();
		QCOMPARE(parts.size(), 2);
		QCOMPARE(parts[0].toObject()["text"].toString(), QString::fromUtf8("😀 "));
		QCOMPARE(parts[1].toObject()["name"].toString(), QString("Kappa"));
	}

	void twitchUserNoticeAndPing()
	{
		QJsonObject m = twitch::toMessage(
			twitch::parseIrc(
				"@display-name=Fan;msg-id=resub;system-msg=Fan\\ssubscribed\\sfor\\s3\\smonths "
				":tmi.twitch.tv USERNOTICE #c :gracias!"),
			"c");
		QCOMPARE(m["kind"].toString(), QString("highlight"));
		QCOMPARE(m["system"].toString(), QString("Fan subscribed for 3 months"));
		QCOMPARE(twitch::parseIrc("PING :tmi.twitch.tv").params, QStringList({"tmi.twitch.tv"}));
		QVERIFY(twitch::toMessage(twitch::parseIrc(":tmi.twitch.tv 366 justinfan #c :End"), "c").isEmpty());
	}

	void kickMessage()
	{
		QJsonArray parts = kick::splitContent("hola [emote:37226:KEKW] que tal");
		QCOMPARE(parts.size(), 3);
		QCOMPARE(parts[1].toObject()["url"].toString(),
			 QString("https://files.kick.com/emotes/37226/fullsize"));
		QJsonObject data = QJsonDocument::fromJson(fixture(0)).object();
		QJsonObject m = kick::toMessage(data, "canal");
		QCOMPARE(m["author"].toString(), QString("pepe"));
		QCOMPARE(m["color"].toString(), QString("#FF0000"));
		QCOMPARE(m["badges"].toArray(), QJsonArray({"moderator", "vip"}));
	}

	void youtubeMessage()
	{
		QJsonObject action = QJsonDocument::fromJson(fixture(1)).object();
		QJsonObject m = youtube::toMessage(action, "@canal");
		QCOMPARE(m["author"].toString(), QString("Ana"));
		QCOMPARE(m["amount"].toString(), QString("$5.00"));
		QCOMPARE(m["kind"].toString(), QString("highlight"));
		QCOMPARE(m["badges"].toArray(), QJsonArray({"moderator"}));
		QJsonArray parts = m["parts"].toArray();
		QCOMPARE(parts[1].toObject()["text"].toString(), QString::fromUtf8("😀"));
		QCOMPARE(parts[2].toObject()["type"].toString(), QString("emote"));

		int timeout = 0;
		QJsonObject lc = QJsonDocument::fromJson(fixture(2)).object();
		QCOMPARE(youtube::nextContinuation(lc, timeout), QString("abc"));
		QCOMPARE(timeout, 4000);
	}

	void youtubeLivePage()
	{
		const QString live = fixture(3);
		QCOMPARE(youtube::liveVideoFromPage(live), QString("dQw4w9WgXcQ"));
		QVERIFY(youtube::liveVideoFromPage(fixture(4)).isEmpty());
	}
	void youtubeViewers()
	{
		QCOMPARE(youtube::viewersFromPage(fixture(5)), 1234);
		QCOMPARE(youtube::viewersFromPage(fixture(6)), 1234);
		QCOMPARE(youtube::viewersFromPage(fixture(7)), -1); // vídeo grabado
		QCOMPARE(youtube::viewersFromPage("nada"), -1);
	}

	void tiktokFetchResult()
	{
		using tiktok::ProtoWriter;
		const QByteArray user = ProtoWriter().bytes(3, "Ana 🌸").bytes(38, "ana.tok").data();
		const QByteArray chatMsg = ProtoWriter()
						   .bytes(2, user)
						   .bytes(3, "hola desde TikTok")
						   .bytes(18, ProtoWriter().varint(5, 1).data())
						   .data();
		const QByteArray gift = ProtoWriter()
						.bytes(15, ProtoWriter().varint(11, 1).varint(12, 1).bytes(16, "Rosa").data())
						.varint(5, 5)
						.bytes(7, user)
						.data();
		const QByteArray giftEnd = gift + ProtoWriter().varint(9, 1).data();
		auto event = [](const char *method, const QByteArray &payload) {
			return ProtoWriter().bytes(1, method).bytes(2, payload).data();
		};
		const QByteArray data = ProtoWriter()
						.bytes(1, event("WebcastChatMessage", chatMsg))
						.bytes(1, event("WebcastGiftMessage", gift))
						.bytes(1, event("WebcastGiftMessage", giftEnd))
						.bytes(1, event("WebcastMemberMessage", "x"))
						.bytes(5, "ext")
						.bytes(7, ProtoWriter().bytes(1, "cursor").bytes(2, "c1").data())
						.varint(9, 1)
						.bytes(10, "wss://webcast/ws")
						.data();

		tiktok::FetchResult r = tiktok::parseFetchResult(data);
		QCOMPARE(r.events.size(), 4);
		QCOMPARE(r.internalExt, QString("ext"));
		QCOMPARE(r.pushServer, QString("wss://webcast/ws"));
		QVERIFY(r.needAck);
		QCOMPARE(r.routeParams.size(), 1);
		QCOMPARE(r.routeParams[0].first, QString("cursor"));

		QJsonObject m = tiktok::toMessage(r.events[0], "@canal");
		QCOMPARE(m["platform"].toString(), QString("tiktok"));
		QCOMPARE(m["author"].toString(), QString::fromUtf8("Ana 🌸"));
		QCOMPARE(m["parts"].toArray()[0].toObject()["text"].toString(), QString("hola desde TikTok"));
		QCOMPARE(m["badges"].toArray(), QJsonArray({"moderator"}));
		QVERIFY(tiktok::toMessage(r.events[1], "@canal").isEmpty()); // regalo en racha
		QJsonObject g = tiktok::toMessage(r.events[2], "@canal");
		QCOMPARE(g["kind"].toString(), QString("highlight"));
		QVERIFY(g["system"].toString().contains(QString::fromUtf8("5× Rosa")));
		QCOMPARE(g["amount"].toString(), QString::fromUtf8("5 💎"));
		QVERIFY(tiktok::toMessage(r.events[3], "@canal").isEmpty());

		// Datos truncados o basura: no debe colgarse ni salirse del búfer.
		for (int cut = 0; cut < data.size(); ++cut)
			tiktok::parseFetchResult(QByteArrayView(data).first(cut));
		tiktok::parseFetchResult(QByteArray("\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff", 11));
	}

	void tiktokRoom()
	{
		QString room;
		LiveResult r = tiktok::parseRoom(
			QJsonDocument::fromJson("{\"data\":{\"user\":{\"roomId\":\"7311\"},\"liveRoom\":{\"status\":2,"
						"\"liveRoomStats\":{\"userCount\":321}}}}")
				.object(),
			room);
		QCOMPARE(room, QString("7311"));
		QCOMPARE(r.live, 1);
		QCOMPARE(r.viewers, 321);
		r = tiktok::parseRoom(QJsonDocument::fromJson("{\"data\":{\"user\":{\"roomId\":\"7311\"},"
							      "\"liveRoom\":{\"status\":4}}}")
					      .object(),
				      room);
		QCOMPARE(r.live, 0);
		QCOMPARE(r.viewers, -1);
	}
	void links()
	{
		QCOMPARE(linkifyHtml("hola <b>"), QString("hola &lt;b&gt;"));
		QCOMPARE(linkifyHtml("mira https://kick.com/canal."),
			 QString("mira <a href=\"https://kick.com/canal\">https://kick.com/canal</a>."));
		QCOMPARE(linkifyHtml("(www.youtube.com/@x)"),
			 QString("(<a href=\"https://www.youtube.com/@x\">www.youtube.com/@x</a>)"));
		QCOMPARE(linkifyHtml("https://es.wikipedia.org/wiki/OBS_(software)"),
			 QString("<a href=\"https://es.wikipedia.org/wiki/OBS_(software)\">"
				 "https://es.wikipedia.org/wiki/OBS_(software)</a>"));
		QCOMPARE(linkifyHtml("a&b http://x.io/?a=1&b=2", "#00f"),
			 QString("a&amp;b <a href=\"http://x.io/?a=1&amp;b=2\" style=\"color:#00f;\">"
				 "http://x.io/?a=1&amp;b=2</a>"));
		QVERIFY(!linkifyHtml("javascript:alert(1) ftp://x").contains("<a"));
	}
	void profiles()
	{
		Profile y = youtube::profileFromPage(fixture(8));
		QCOMPARE(y.name, QString("Alejo & Tech"));
		QCOMPARE(y.avatar, QString("https://yt3.googleusercontent.com/abc123=s88-c-k-c0x00ffffff-no-rp"));
		Profile v = youtube::profileFromPage(fixture(9));
		QCOMPARE(v.name, QString("Mi Canal"));
		QCOMPARE(v.avatar, QString("https://yt3.ggpht.com/xyz=s88-c-k-c0x00ffffff-no-rp"));

		Profile t = twitch::profileFromGql(QJsonDocument::fromJson(
			"{\"data\":{\"user\":{\"displayName\":\"MiCanal\",\"profileImageURL\":\"https://static-cdn.jtvnw.net/a.png\"}}}").object());
		QCOMPARE(t.name, QString("MiCanal"));
		QCOMPARE(t.avatar, QString("https://static-cdn.jtvnw.net/a.png"));

		Profile k = kick::profileFromChannel(QJsonDocument::fromJson(
			"{\"user\":{\"username\":\"alejoxcode\",\"profile_pic\":\"https://files.kick.com/p.webp\"}}").object());
		QCOMPARE(k.name, QString("alejoxcode"));
		QCOMPARE(k.avatar, QString("https://files.kick.com/p.webp"));

		Profile tt = tiktok::profileFromRoom(QJsonDocument::fromJson(
			"{\"data\":{\"user\":{\"nickname\":\"Alejo\",\"avatarThumb\":\"https://p16.tiktokcdn.com/a.jpeg\"}}}").object());
		QCOMPARE(tt.name, QString("Alejo"));
		QCOMPARE(tt.avatar, QString("https://p16.tiktokcdn.com/a.jpeg"));
	}
	void updates()
	{
		QVERIFY(isNewerVersion("v2.0.2", "2.0.1"));
		QVERIFY(isNewerVersion("2.0.10", "2.0.9"));
		QVERIFY(isNewerVersion("2.1", "2.0.9"));
		QVERIFY(!isNewerVersion("v2.0.1", "2.0.1"));
		QVERIFY(!isNewerVersion("2.0.0", "2.0.1"));
		QVERIFY(!isNewerVersion("", "2.0.1"));
		const QJsonObject json = QJsonDocument::fromJson(
			"{\"tag_name\":\"v2.0.2\",\"html_url\":\"https://github.com/r/releases/tag/v2.0.2\",\"assets\":["
			"{\"name\":\"obs-multichat-2.0.2-macos.zip\",\"browser_download_url\":\"https://d/mac.zip\"},"
			"{\"name\":\"obs-multichat-2.0.2-windows-x64.zip\",\"browser_download_url\":\"https://d/win.zip\"}]}").object();
		Release r = parseLatestRelease(json, "-windows-x64.zip");
		QCOMPARE(r.version, QString("2.0.2"));
		QCOMPARE(r.url, QString("https://d/win.zip"));
		QCOMPARE(parseLatestRelease(json, "-linux.zip").url, QString("https://github.com/r/releases/tag/v2.0.2"));
		QJsonObject draft = json;
		draft.insert("prerelease", true);
		QVERIFY(parseLatestRelease(draft, "-macos.zip").version.isEmpty());
	}
};

QTEST_GUILESS_MAIN(ChatTest)
#include "test-chat.moc"
