// Kick: WebSocket público de Pusher.

#include "platforms.hpp"
#include "message.hpp"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>

namespace chat {
namespace kick {

namespace {

// Kick cambia la clave de Pusher sin avisar: se pide a su API y esta queda de reserva.
const char *const kFallbackKey = "34bf7a0ff419a2a775b9";
const char *const kFallbackCluster = "us2";

QString pusherUrl(const QString &key, const QString &cluster)
{
	return QStringLiteral("wss://ws-%1.pusher.com/app/%2?protocol=7&client=js&version=8.4.0&flash=false")
		.arg(cluster, key);
}

// Pregunta a Kick qué clave de Pusher usa ahora (lo mismo que hace su web).
QString resolvePusherUrl()
{
	const QJsonObject body{
		{"client", QJsonObject{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"type", "web"}}},
		{"capabilities",
		 QJsonObject{{"accepted_providers", QJsonArray{QJsonObject{{"provider", "pusher"}}}}}},
	};
	net::Response res = net::postJson("https://web.kick.com/api/v1/realtime/connection",
					  QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString(),
					  {{"Accept", "application/json"}});
	if (res.ok()) {
		const QJsonObject data =
			QJsonDocument::fromJson(QByteArray::fromStdString(res.body)).object().value("data").toObject();
		for (const QJsonValue c : data.value("connections").toArray()) {
			const QJsonObject conn = c.toObject();
			const QJsonObject cred = conn.value("credentials").toObject();
			const QString key = cred.value("app_key").toString();
			const QString cluster = cred.value("cluster").toString();
			if (conn.value("provider").toString() == "pusher" && !key.isEmpty() && !cluster.isEmpty())
				return pusherUrl(key, cluster);
		}
	}
	return pusherUrl(kFallbackKey, kFallbackCluster);
}

QString emoteUrl(const QString &id)
{
	return QStringLiteral("https://files.kick.com/emotes/%1/fullsize").arg(id);
}

// Datos del canal (id del chatroom y si está en vivo). Kick a veces bloquea esta API.
QJsonObject channelInfo(const QString &slug, QString &error)
{
	for (const char *version : {"v2", "v1"}) {
		net::Response res = net::get(QStringLiteral("https://kick.com/api/%1/channels/%2")
						     .arg(QString::fromLatin1(version), slug)
						     .toStdString(),
					     {{"Accept", "application/json"}});
		if (res.ok()) {
			QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(res.body));
			if (doc.isObject())
				return doc.object();
		}
		error = res.status ? QStringLiteral("HTTP %1").arg(res.status) : QString::fromStdString(res.error);
	}
	return {};
}

} // namespace

QJsonArray splitContent(const QString &content)
{
	static const QRegularExpression re(QStringLiteral("\\[emote:(\\d+):([^\\]]*)\\]"));
	QJsonArray parts;
	qsizetype cur = 0;
	auto it = re.globalMatch(content);
	while (it.hasNext()) {
		auto m = it.next();
		if (m.capturedStart() > cur)
			parts.append(textPart(content.mid(cur, m.capturedStart() - cur)));
		parts.append(emotePart(emoteUrl(m.captured(1)), m.captured(2)));
		cur = m.capturedEnd();
	}
	if (cur < content.size())
		parts.append(textPart(content.mid(cur)));
	return parts;
}

QJsonObject toMessage(const QJsonObject &data, const QString &channel)
{
	const QJsonObject sender = data.value("sender").toObject();
	const QJsonObject identity = sender.value("identity").toObject();
	MessageOptions o;
	o.color = identity.value("color").toString();
	for (const QJsonValue b : identity.value("badges").toArray()) {
		const QString type = b.toObject().value("type").toString();
		QString name;
		if (type == "broadcaster")
			name = "owner";
		else if (type == "moderator" || type == "vip" || type == "subscriber" || type == "verified" ||
			 type == "staff")
			name = type;
		else if (type == "og")
			name = "vip";
		else if (type == "founder")
			name = "subscriber";
		if (!name.isEmpty() && !o.badges.contains(name))
			o.badges << name;
	}
	const QJsonValue id = data.value("id");
	o.id = id.isString() ? id.toString() : (id.isDouble() ? QString::number(id.toInteger()) : QString());
	return makeMessage("kick", channel, sender.value("username").toString("?"),
			   splitContent(data.value("content").toString()), o);
}

LiveResult checkLive(const QString &channel)
{
	LiveResult r;
	QString error;
	QJsonObject info = channelInfo(channel, error);
	if (!info.isEmpty())
		r.live = info.value("livestream").isObject() ? 1 : 0;
	return r;
}

class KickConnector : public Connector {
public:
	using Connector::Connector;

protected:
	void session() override
	{
		if (chatroom_.isEmpty())
			chatroom_ = source_.extra;
		if (chatroom_.isEmpty()) {
			QString error;
			QJsonObject info = channelInfo(source_.channel, error);
			const QJsonValue id = info.value("chatroom").toObject().value("id");
			if (id.isDouble())
				chatroom_ = QString::number(id.toInteger());
			if (chatroom_.isEmpty())
				throw ChatError(QStringLiteral("no se pudo consultar el canal de Kick (%1). "
							       "Indica el chat a mano: kick:%2#ID")
							.arg(error.isEmpty() ? QStringLiteral("sin chat") : error,
							     source_.channel));
		}

		if (url_.isEmpty())
			url_ = resolvePusherUrl();
		auto ws = openSocket(url_.toStdString(), {{"Origin", "https://kick.com"}});
		QJsonObject subscribe{{"event", "pusher:subscribe"},
				      {"data",
				       QJsonObject{{"auth", ""}, {"channel", "chatrooms." + chatroom_ + ".v2"}}}};
		ws->send(QJsonDocument(subscribe).toJson(QJsonDocument::Compact).toStdString());

		std::string raw;
		while (!stopped()) {
			if (ws->receive(raw) == net::WebSocket::Result::Closed) {
				if (stopped())
					return;
				url_.clear();
				throw ChatError("Kick cerró la conexión");
			}
			const QJsonObject evt = QJsonDocument::fromJson(QByteArray::fromStdString(raw)).object();
			const QString name = evt.value("event").toString();
			if (name == "pusher:ping") {
				ws->send(R"({"event":"pusher:pong","data":{}})");
			} else if (name == "pusher_internal:subscription_succeeded") {
				report_("connected", QString());
			} else if (name == "pusher:error") {
				url_.clear(); // quizá cambió la clave: se vuelve a pedir al reconectar
				QJsonValue data = evt.value("data");
				const QJsonObject err = data.isString()
								? QJsonDocument::fromJson(data.toString().toUtf8()).object()
								: data.toObject();
				throw ChatError(QStringLiteral("Pusher %1: %2")
							.arg(err.value("code").toInt())
							.arg(err.value("message").toString()));
			} else if (name == "App\\Events\\ChatMessageEvent") {
				QJsonValue data = evt.value("data");
				QJsonObject obj = data.isString()
							  ? QJsonDocument::fromJson(data.toString().toUtf8()).object()
							  : data.toObject();
				post_(toMessage(obj, source_.channel));
			}
		}
	}

private:
	QString chatroom_; // se conserva entre reconexiones
	QString url_;      // ídem, salvo si Pusher la rechaza
};

std::shared_ptr<Connector> create(const SourceInfo &s, Connector::PostFn post, Connector::ReportFn report)
{
	return std::make_shared<KickConnector>(s, std::move(post), std::move(report));
}

} // namespace kick
} // namespace chat
