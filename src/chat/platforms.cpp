#include "platforms.hpp"
#include "message.hpp"

#include <QRandomGenerator>
#include <QRegularExpression>

#include <iterator>

namespace chat {

LiveResult checkLive(const SourceInfo &s)
{
	if (s.platform == "twitch")
		return twitch::checkLive(s.channel);
	if (s.platform == "kick")
		return kick::checkLive(s.channel);
	if (s.platform == "youtube")
		return youtube::checkLive(s);
	if (s.platform == "tiktok")
		return tiktok::checkLive(s.channel);
	if (s.platform == "demo")
	{
		LiveResult r;
		r.live = 1;
		return r;
	}
	return {};
}

std::shared_ptr<Connector> createConnector(const SourceInfo &s, Connector::PostFn post, Connector::ReportFn report)
{
	if (s.platform == "twitch")
		return twitch::create(s, std::move(post), std::move(report));
	if (s.platform == "kick")
		return kick::create(s, std::move(post), std::move(report));
	if (s.platform == "youtube")
		return youtube::create(s, std::move(post), std::move(report));
	if (s.platform == "tiktok")
		return tiktok::create(s, std::move(post), std::move(report));
	return demo::create(s, std::move(post), std::move(report));
}

namespace {

SourceInfo make(const QString &platform, const QString &channel, const QString &extra = {}, bool videoFixed = false)
{
	SourceInfo s;
	s.platform = platform;
	s.channel = channel;
	s.extra = extra;
	s.videoFixed = videoFixed;
	s.key = (platform + ':' + channel).toLower();
	return s;
}

bool youtubeChannel(const QString &channel, SourceInfo &out)
{
	const QString video = youtube::parseVideoId(channel);
	if (!video.isEmpty()) {
		out = make("youtube", video, video, true);
		return true;
	}
	if (channel.startsWith("UC") && channel.size() == 24)
		out = make("youtube", channel);
	else
		out = make("youtube", channel.startsWith('@') ? channel : '@' + channel);
	return true;
}

// Usuario de TikTok: letras, números, "_" y ".".
bool tiktokChannel(QString user, SourceInfo &out, QString &error)
{
	if (user.startsWith('@'))
		user.remove(0, 1);
	static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_.]{2,24}$"));
	if (!valid.match(user).hasMatch()) {
		error = QStringLiteral("usuario de TikTok no válido: %1").arg(user);
		return false;
	}
	out = make("tiktok", '@' + user.toLower());
	return true;
}

} // namespace

bool parseEntry(const QString &entry, SourceInfo &out, QString &error)
{
	const QString raw = entry.trimmed();
	if (raw.isEmpty()) {
		error = QStringLiteral("entrada vacía");
		return false;
	}

	// URLs: twitch.tv/canal, kick.com/canal, youtube.com/@canal, youtube.com/watch?v=..., tiktok.com/@usuario
	static const QRegularExpression scheme(QStringLiteral("^https?://(www\\.|m\\.)?"),
					       QRegularExpression::CaseInsensitiveOption);
	QString url = raw;
	url.remove(scheme);
	const QStringList seg = url.split('/', Qt::SkipEmptyParts);
	const QString host = seg.value(0).toLower();
	if (host == "twitch.tv" && seg.size() > 1) {
		out = make("twitch", seg[1].toLower());
		return true;
	}
	if (host == "kick.com" && seg.size() > 1) {
		out = make("kick", seg[1].toLower());
		return true;
	}
	if (host == "tiktok.com" && seg.size() > 1 && seg[1].startsWith('@'))
		return tiktokChannel(seg[1], out, error);
	if (host == "youtube.com" || host == "youtu.be") {
		if (!youtube::parseVideoId(url).isEmpty())
			return youtubeChannel(url, out);
		if (seg.size() > 2 && seg[1] == "channel")
			return youtubeChannel(seg[2], out);
		if (seg.size() > 1 && seg[1].startsWith('@'))
			return youtubeChannel(seg[1], out);
		error = QStringLiteral("URL de YouTube no reconocida: %1").arg(raw);
		return false;
	}

	const qsizetype colon = raw.indexOf(':');
	QString platform = raw.left(colon).trimmed().toLower();
	QString channel = colon < 0 ? QString() : raw.mid(colon + 1).trimmed();
	if (platform == "yt")
		platform = "youtube";
	if (colon < 0 || channel.isEmpty()) {
		error = QStringLiteral("usa el formato plataforma:canal (ej. twitch:micanal): %1").arg(raw);
		return false;
	}
	if (platform == "twitch") {
		if (channel.startsWith('#'))
			channel.remove(0, 1);
		out = make("twitch", channel.toLower());
		return true;
	}
	if (platform == "kick") {
		// kick:canal#12345 indica a mano el id del chat (si Kick bloquea su API).
		const QString slug = channel.section('#', 0, 0).toLower();
		out = make("kick", slug, channel.section('#', 1));
		return true;
	}
	if (platform == "youtube")
		return youtubeChannel(channel, out);
	if (platform == "tiktok" || platform == "tt")
		return tiktokChannel(channel, out, error);
	if (platform == "demo") {
		out = make("demo", channel);
		return true;
	}
	error = QStringLiteral("plataforma no soportada: %1 (usa twitch, youtube, kick o tiktok)").arg(platform);
	return false;
}

// Mensajes de ejemplo para probar el dock y el overlay sin estar en directo.
namespace demo {

class DemoConnector : public Connector {
public:
	using Connector::Connector;

protected:
	void session() override
	{
		struct User {
			const char *platform, *name, *color, *badge;
		};
		static const User users[] = {
			{"twitch", "LunaGamer", "#FF7F50", "subscriber"},
			{"twitch", "pixel_pancho", "#1E90FF", "moderator"},
			{"twitch", "NachoVIP", "#DAA520", "vip"},
			{"youtube", "María Fernández", "", "member"},
			{"youtube", "Carlos R.", "", ""},
			{"youtube", "Canal Oficial", "", "owner"},
			{"kick", "kicker_mx", "#53FC18", ""},
			{"kick", "zeta_arg", "#FF69B4", "subscriber"},
			{"tiktok", "vale.tok", "", ""},
			{"tiktok", "rodri_live", "", "moderator"},
		};
		static const char *lines[] = {
			"¡Holaaa desde Buenos Aires!",
			"qué buen stream hoy",
			"jajaja",
			"GG",
			"ese clutch estuvo increíble",
			"¿a qué hora termina el directo?",
			"saludos a todos los del chat de Kick",
			"primera vez por aquí 👋",
			"LUL",
			"eso fue épico",
			"¿qué teclado usas?",
			"vamos que se puede 💪",
		};
		auto *rng = QRandomGenerator::global();
		report_("connected", QString());
		while (!wait(rng->bounded(400, 1800))) {
			const User &u = users[rng->bounded(int(std::size(users)))];
			MessageOptions o;
			o.color = QString::fromLatin1(u.color);
			if (*u.badge)
				o.badges << QString::fromLatin1(u.badge);
			QJsonArray parts{textPart(QString::fromUtf8(lines[rng->bounded(int(std::size(lines)))]))};
			if (QString::fromLatin1(u.platform) == "twitch" && rng->bounded(10) < 3) {
				parts.append(textPart(" "));
				parts.append(emotePart("https://static-cdn.jtvnw.net/emoticons/v2/25/default/dark/2.0",
						       "Kappa"));
			}
			const int roll = rng->bounded(100);
			const QString name = QString::fromUtf8(u.name);
			if (roll < 5) {
				o.kind = "highlight";
				o.amount = QString::fromLatin1(
					QList<const char *>{"$5.00", "$20.00", "500 bits"}[rng->bounded(3)]);
			} else if (roll < 8) {
				o.kind = "highlight";
				o.system = name + " se suscribió por 6 meses";
				parts = QJsonArray();
			}
			post_(makeMessage(QString::fromLatin1(u.platform), source_.channel, name, parts, o));
		}
	}
};

std::shared_ptr<Connector> create(const SourceInfo &s, Connector::PostFn post, Connector::ReportFn report)
{
	return std::make_shared<DemoConnector>(s, std::move(post), std::move(report));
}

} // namespace demo
} // namespace chat
