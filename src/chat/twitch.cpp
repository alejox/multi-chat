// Twitch: IRC sobre WebSocket, anónimo y de solo lectura.

#include "platforms.hpp"
#include "message.hpp"

#include <QJsonDocument>
#include <QRandomGenerator>

#include <algorithm>
#include <tuple>
#include <vector>

namespace chat {
namespace twitch {

namespace {

const char *const kIrcUrl = "wss://irc-ws.chat.twitch.tv:443";
// Client-ID público del sitio web de Twitch, para saber si un canal está en vivo sin cuenta.
const char *const kGqlClientId = "kimne78kx3ncx6brgo4mv6wki5h1ko";

QString emoteUrl(const QString &id)
{
	return QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/%1/default/dark/2.0").arg(id);
}

QString unescapeTag(const QString &v)
{
	QString out;
	out.reserve(v.size());
	for (int i = 0; i < v.size(); ++i) {
		QChar c = v[i];
		if (c == '\\' && i + 1 < v.size()) {
			QChar n = v[++i];
			if (n == 's')
				out += ' ';
			else if (n == ':')
				out += ';';
			else if (n == 'r')
				out += '\r';
			else if (n == 'n')
				out += '\n';
			else
				out += n;
		} else {
			out += c;
		}
	}
	return out;
}

QStringList badges(const QString &tag)
{
	QStringList out;
	for (const QString &b : tag.split(',', Qt::SkipEmptyParts)) {
		QString name = b.section('/', 0, 0);
		if (name == "broadcaster")
			name = "owner";
		else if (name == "founder")
			name = "subscriber";
		else if (name == "partner")
			name = "verified";
		else if (name != "moderator" && name != "vip" && name != "subscriber" && name != "staff")
			continue;
		if (!out.contains(name))
			out << name;
	}
	return out;
}

} // namespace

IrcMessage parseIrc(const QString &input)
{
	IrcMessage msg;
	QString line = input;
	if (line.startsWith('@')) {
		qsizetype sp = line.indexOf(' ');
		const QString raw = line.mid(1, sp < 0 ? -1 : sp - 1);
		line = sp < 0 ? QString() : line.mid(sp + 1);
		for (const QString &kv : raw.split(';')) {
			qsizetype eq = kv.indexOf('=');
			if (eq < 0)
				msg.tags.insert(kv, QString());
			else
				msg.tags.insert(kv.left(eq), unescapeTag(kv.mid(eq + 1)));
		}
	}
	if (line.startsWith(':')) {
		qsizetype sp = line.indexOf(' ');
		msg.prefix = line.mid(1, sp < 0 ? -1 : sp - 1);
		line = sp < 0 ? QString() : line.mid(sp + 1);
	}
	QString trailing;
	bool hasTrailing = false;
	qsizetype t = line.indexOf(" :");
	if (t >= 0) {
		trailing = line.mid(t + 2);
		line = line.left(t);
		hasTrailing = true;
	} else if (line.startsWith(':')) {
		trailing = line.mid(1);
		line.clear();
		hasTrailing = true;
	}
	msg.params = line.split(' ', Qt::SkipEmptyParts);
	if (!msg.params.isEmpty())
		msg.command = msg.params.takeFirst();
	if (hasTrailing)
		msg.params << trailing;
	return msg;
}

QJsonArray splitEmotes(const QString &body, const QString &emotesTag)
{
	// Twitch da las posiciones en puntos de código, no en unidades UTF-16.
	const QList<uint> cps = body.toUcs4();
	std::vector<std::tuple<int, int, QString>> ranges;
	for (const QString &spec : emotesTag.split('/', Qt::SkipEmptyParts)) {
		const QString id = spec.section(':', 0, 0);
		for (const QString &pos : spec.section(':', 1).split(',', Qt::SkipEmptyParts)) {
			bool ok1 = false, ok2 = false;
			int a = pos.section('-', 0, 0).toInt(&ok1);
			int b = pos.section('-', 1, 1).toInt(&ok2);
			if (ok1 && ok2)
				ranges.emplace_back(a, b, id);
		}
	}
	std::sort(ranges.begin(), ranges.end());

	auto slice = [&](qsizetype from, qsizetype to) {
		return QString::fromUcs4(reinterpret_cast<const char32_t *>(cps.constData()) + from, to - from);
	};
	QJsonArray parts;
	qsizetype cur = 0;
	for (const auto &[a, b, id] : ranges) {
		if (a < cur || b >= cps.size() || b < a)
			continue;
		if (a > cur)
			parts.append(textPart(slice(cur, a)));
		parts.append(emotePart(emoteUrl(id), slice(a, b + 1)));
		cur = b + 1;
	}
	if (cur < cps.size())
		parts.append(textPart(slice(cur, cps.size())));
	return parts;
}

QJsonObject toMessage(const IrcMessage &msg, const QString &channel)
{
	QString author = msg.tags.value("display-name");
	if (author.isEmpty())
		author = msg.prefix.section('!', 0, 0);

	MessageOptions o;
	o.color = msg.tags.value("color");
	o.badges = badges(msg.tags.value("badges"));
	o.id = msg.tags.value("id");

	if (msg.command == "PRIVMSG") {
		QString body = msg.params.isEmpty() ? QString() : msg.params.last();
		if (body.startsWith("\x01"
				    "ACTION ") &&
		    body.endsWith('\x01')) {
			body = body.mid(8, body.size() - 9);
			o.action = true;
		}
		const QString bits = msg.tags.value("bits");
		if (!bits.isEmpty()) {
			o.kind = "highlight";
			o.amount = bits + " bits";
		}
		return makeMessage("twitch", channel, author, splitEmotes(body, msg.tags.value("emotes")), o);
	}
	if (msg.command == "USERNOTICE") {
		const QString body = msg.params.size() > 1 ? msg.params.last() : QString();
		o.kind = "highlight";
		o.system = msg.tags.value("system-msg");
		return makeMessage("twitch", channel, author,
				   body.isEmpty() ? QJsonArray() : splitEmotes(body, msg.tags.value("emotes")), o);
	}
	return {};
}

LiveResult checkLive(const QString &channel)
{
	LiveResult r;
	QString login;
	for (QChar c : channel.toLower())
		if (c.isLetterOrNumber() || c == '_')
			login += c;
	QJsonObject query{{"query", QStringLiteral("query{user(login:\"%1\"){stream{id}}}").arg(login)}};
	net::Response res = net::postJson("https://gql.twitch.tv/gql",
					  QJsonDocument(query).toJson(QJsonDocument::Compact).toStdString(),
					  {{"Client-Id", kGqlClientId}});
	if (!res.ok())
		return r;
	QJsonObject user = QJsonDocument::fromJson(QByteArray::fromStdString(res.body))
				   .object()
				   .value("data")
				   .toObject()
				   .value("user")
				   .toObject();
	if (user.isEmpty())
		return r;
	r.live = user.value("stream").isObject() ? 1 : 0;
	return r;
}

class TwitchConnector : public Connector {
public:
	using Connector::Connector;

protected:
	void session() override
	{
		const QString chan = source_.channel.toLower();
		auto ws = openSocket(kIrcUrl);
		ws->send("CAP REQ :twitch.tv/tags twitch.tv/commands");
		ws->send("PASS SCHMOOPIIE");
		ws->send("NICK justinfan" + std::to_string(QRandomGenerator::global()->bounded(10000, 99999)));
		ws->send("JOIN #" + chan.toStdString());

		std::string data;
		while (!stopped()) {
			if (ws->receive(data) == net::WebSocket::Result::Closed) {
				if (stopped())
					return;
				throw ChatError("Twitch cerró la conexión");
			}
			for (const QString &line : QString::fromUtf8(data).split("\r\n", Qt::SkipEmptyParts)) {
				IrcMessage msg = parseIrc(line);
				if (msg.command == "PING") {
					ws->send("PONG :" + (msg.params.isEmpty() ? std::string("tmi.twitch.tv")
										  : msg.params.last().toStdString()));
				} else if (msg.command == "ROOMSTATE" || msg.command == "366") {
					report_("connected", QString());
				} else if (msg.command == "RECONNECT") {
					throw ChatError("Twitch pidió reconectar");
				} else if (msg.command == "NOTICE" &&
					   msg.tags.value("msg-id").startsWith("msg_channel_suspended")) {
					throw ChatError("canal suspendido o inexistente");
				} else {
					QJsonObject m = toMessage(msg, chan);
					if (!m.isEmpty())
						post_(m);
				}
			}
		}
	}
};

std::shared_ptr<Connector> create(const SourceInfo &s, Connector::PostFn post, Connector::ReportFn report)
{
	return std::make_shared<TwitchConnector>(s, std::move(post), std::move(report));
}

} // namespace twitch
} // namespace chat
