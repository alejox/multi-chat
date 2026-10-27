// Pruebas de los parsers (sin red ni OBS). Se compilan en CI y con tests/CMakeLists.txt.

#include "../src/chat/message.hpp"
#include "../src/chat/platforms.hpp"

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

private slots:
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

		SourceInfo s;
		QString error;
		for (const char *bad : {"canal", "facebook:x", "twitch:", ""})
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
};

QTEST_GUILESS_MAIN(ChatTest)
#include "test-chat.moc"
