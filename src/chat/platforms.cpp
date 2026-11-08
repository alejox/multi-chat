#include "platforms.hpp"
#include "message.hpp"

#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

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
	if (s.platform == "starvios")
		return starvios::checkLive(s.channel);
	if (s.platform == "demo")
	{
		LiveResult r;
		r.live = 1;
		return r;
	}
	return {};
}

Profile fetchProfile(const SourceInfo &s)
{
	if (s.platform == "twitch")
		return twitch::fetchProfile(s.channel);
	if (s.platform == "kick")
		return kick::fetchProfile(s.channel);
	if (s.platform == "youtube")
		return youtube::fetchProfile(s);
	if (s.platform == "tiktok")
		return tiktok::fetchProfile(s.channel);
	if (s.platform == "starvios")
		return starvios::fetchProfile(s.channel);
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
	if (s.platform == "starvios")
		return starvios::create(s, std::move(post), std::move(report));
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

// Twitch login: letters, numbers and "_". Kick slug: also "-". Lenient on length (old accounts).
bool twitchChannel(QString user, SourceInfo &out, EntryError &error, QString &arg)
{
	if (user.startsWith('#'))
		user.remove(0, 1);
	static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_]{1,25}$"));
	if (!valid.match(user).hasMatch()) {
		error = EntryError::TwitchUser;
		arg = user;
		return false;
	}
	out = make("twitch", user.toLower());
	return true;
}

bool kickChannel(const QString &channel, SourceInfo &out, EntryError &error, QString &arg)
{
	// kick:canal#12345 indica a mano el id del chat (si Kick bloquea su API).
	const QString slug = channel.section('#', 0, 0);
	static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{1,50}$"));
	if (!valid.match(slug).hasMatch()) {
		error = EntryError::KickUser;
		arg = slug;
		return false;
	}
	out = make("kick", slug.toLower(), channel.section('#', 1));
	return true;
}

// Usuario de TikTok: letras, números, "_" y ".".
bool tiktokChannel(QString user, SourceInfo &out, EntryError &error, QString &arg)
{
	if (user.startsWith('@'))
		user.remove(0, 1);
	static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_.]{2,24}$"));
	if (!valid.match(user).hasMatch()) {
		error = EntryError::TikTokUser;
		arg = user;
		return false;
	}
	out = make("tiktok", '@' + user.toLower());
	return true;
}

// Usuario de Starvios: la misma regla que aplica su web. Va a URLs y al nombre visible del canal.
bool starviosChannel(QString user, SourceInfo &out, EntryError &error, QString &arg)
{
	if (user.startsWith('@'))
		user.remove(0, 1);
	static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_]{3,25}$"));
	if (!valid.match(user).hasMatch()) {
		error = EntryError::StarviosUser;
		arg = user;
		return false;
	}
	out = make("starvios", user.toLower());
	return true;
}

// Primeras partes de ruta de la web de Starvios que no son canales, sacadas de las rutas de su bundle:
// una ruta fija gana a /$username, así que en esas direcciones nunca hay un canal.
bool starviosReservedPath(const QString &segment)
{
	static const QSet<QString> reserved{
		"admin", "alerts", "api", "assets", "auth", "canal", "clip", "clips", "directo", "directos", "earn", "escena",
		"explorar", "explore", "flash", "following", "guion", "history", "league", "live", "overlay", "patrocinios",
		"playlist", "playlists", "popout", "privacy", "search", "settings", "sponsors", "starvies", "starviosparty",
		"studio", "support", "terms", "tienda", "ver", "watch",
		// Con guion no pasarían la regla de los usuarios, pero también son rutas de la web.
		"forgot-password", "go-live", "league-admin", "livekit-test", "partner-review", "promo-management",
		"reset-password", "watch-later",
	};
	return reserved.contains(segment.toLower());
}

// starvios.com/<usuario> y las rutas que llevan a ese canal: /canal/<usuario> redirige ahí, /live/<usuario>
// es el directo y /directo/<usuario> redirige al directo. La web no usa "@" en ninguna de ellas.
bool starviosUrl(const QString &url, const QString &raw, SourceInfo &out, EntryError &error, QString &arg)
{
	const QStringList path = url.section('?', 0, 0).section('#', 0, 0).split('/', Qt::SkipEmptyParts);
	QString user;
	if (path.size() == 2 && !starviosReservedPath(path[1])) {
		user = path[1];
	} else if (path.size() == 3) {
		const QString route = path[1].toLower();
		if (route == "live" || route == "directo" || route == "canal")
			user = path[2];
	}
	if (user.isEmpty() || user.startsWith('@')) {
		error = EntryError::StarviosUrl;
		arg = raw;
		return false;
	}
	return starviosChannel(user, out, error, arg);
}

} // namespace

bool parseEntry(const QString &entry, SourceInfo &out, EntryError &error, QString &arg)
{
	error = EntryError::None;
	arg.clear();
	const QString raw = entry.trimmed();
	if (raw.isEmpty()) {
		error = EntryError::Empty;
		return false;
	}

	// URLs: twitch.tv/canal, kick.com/canal, youtube.com/@canal, youtube.com/watch?v=..., tiktok.com/@usuario,
	// starvios.com/usuario, starvios.com/live/usuario
	static const QRegularExpression scheme(QStringLiteral("^https?://(www\\.|m\\.)?"),
					       QRegularExpression::CaseInsensitiveOption);
	QString url = raw;
	url.remove(scheme);
	const QStringList seg = url.split('/', Qt::SkipEmptyParts);
	const QString host = seg.value(0).toLower();
	if (host == "twitch.tv" && seg.size() > 1)
		return twitchChannel(seg[1].section('?', 0, 0).section('#', 0, 0), out, error, arg);
	if (host == "kick.com" && seg.size() > 1)
		return kickChannel(seg[1].section('?', 0, 0).section('#', 0, 0), out, error, arg);
	if (host == "tiktok.com" && seg.size() > 1 && seg[1].startsWith('@'))
		return tiktokChannel(seg[1], out, error, arg);
	if (host == "starvios.com" || host == "www.starvios.com")
		return starviosUrl(url, raw, out, error, arg);
	if (host == "youtube.com" || host == "youtu.be") {
		if (!youtube::parseVideoId(url).isEmpty())
			return youtubeChannel(url, out);
		if (seg.size() > 2 && seg[1] == "channel")
			return youtubeChannel(seg[2], out);
		if (seg.size() > 1 && seg[1].startsWith('@'))
			return youtubeChannel(seg[1], out);
		error = EntryError::YouTubeUrl;
		arg = raw;
		return false;
	}

	const qsizetype colon = raw.indexOf(':');
	QString platform = raw.left(colon).trimmed().toLower();
	QString channel = colon < 0 ? QString() : raw.mid(colon + 1).trimmed();
	if (platform == "yt")
		platform = "youtube";
	if (colon < 0 || channel.isEmpty()) {
		error = EntryError::Format;
		arg = raw;
		return false;
	}
	if (platform == "twitch")
		return twitchChannel(channel, out, error, arg);
	if (platform == "kick")
		return kickChannel(channel, out, error, arg);
	if (platform == "youtube")
		return youtubeChannel(channel, out);
	if (platform == "tiktok" || platform == "tt")
		return tiktokChannel(channel, out, error, arg);
	if (platform == "starvios")
		return starviosChannel(channel, out, error, arg);
	if (platform == "demo") {
		out = make("demo", channel);
		return true;
	}
	error = EntryError::Platform;
	arg = platform;
	return false;
}

const char *entryErrorKey(EntryError error)
{
	switch (error) {
	case EntryError::None:
		break;
	case EntryError::Empty:
		return "Entry.Error.Empty";
	case EntryError::Format:
		return "Entry.Error.Format";
	case EntryError::Platform:
		return "Entry.Error.Platform";
	case EntryError::TwitchUser:
		return "Entry.Error.TwitchUser";
	case EntryError::KickUser:
		return "Entry.Error.KickUser";
	case EntryError::TikTokUser:
		return "Entry.Error.TikTokUser";
	case EntryError::StarviosUser:
		return "Entry.Error.StarviosUser";
	case EntryError::YouTubeUrl:
		return "Entry.Error.YouTubeUrl";
	case EntryError::StarviosUrl:
		return "Entry.Error.StarviosUrl";
	}
	return nullptr;
}

bool parseEntry(const QString &entry, SourceInfo &out, QString &error)
{
	EntryError code = EntryError::None;
	QString arg;
	if (parseEntry(entry, out, code, arg))
		return true;
	switch (code) {
	case EntryError::Empty:
		error = QStringLiteral("empty entry");
		break;
	case EntryError::Format:
		error = QStringLiteral("use the platform:channel format: %1").arg(arg);
		break;
	case EntryError::Platform:
		error = QStringLiteral("unsupported platform: %1").arg(arg);
		break;
	case EntryError::TwitchUser:
		error = QStringLiteral("invalid Twitch channel: %1").arg(arg);
		break;
	case EntryError::KickUser:
		error = QStringLiteral("invalid Kick channel: %1").arg(arg);
		break;
	case EntryError::TikTokUser:
		error = QStringLiteral("invalid TikTok username: %1").arg(arg);
		break;
	case EntryError::StarviosUser:
		error = QStringLiteral("invalid Starvios username: %1").arg(arg);
		break;
	case EntryError::YouTubeUrl:
		error = QStringLiteral("YouTube URL not recognized: %1").arg(arg);
		break;
	case EntryError::StarviosUrl:
		error = QStringLiteral("Starvios URL not recognized: %1").arg(arg);
		break;
	case EntryError::None:
		break;
	}
	return false;
}

QString entryFromInput(const QString &text, const QString &platform)
{
	const QString value = text.trimmed();
	if (value.isEmpty())
		return {};
	// URLs, with or without the scheme.
	static const QRegularExpression url(
		QStringLiteral("^(https?://|(www\\.|m\\.)?(twitch\\.tv|kick\\.com|youtube\\.com|youtu\\.be|tiktok\\.com|"
			       "starvios\\.com)(/|$))"),
		QRegularExpression::CaseInsensitiveOption);
	// "word:" is an explicit platform (an unknown one is reported as unsupported by parseEntry).
	static const QRegularExpression prefix(QStringLiteral("^[A-Za-z]+\\s*:"));
	if (url.match(value).hasMatch() || prefix.match(value).hasMatch())
		return value;
	return platform + ':' + value;
}

QList<ChatEntry> chatEntriesFromJson(const QJsonObject &config)
{
	QSet<QString> disabled;
	for (const QJsonValue v : config.value("disabled").toArray())
		disabled.insert(v.toString().trimmed());
	QList<ChatEntry> out;
	for (const QJsonValue v : config.value("chats").toArray()) {
		ChatEntry e;
		if (v.isObject()) {
			const QJsonObject o = v.toObject();
			e.entry = o.value("entry").toString().trimmed();
			e.enabled = o.value("enabled").toBool(true);
		} else {
			e.entry = v.toString().trimmed();
		}
		if (e.entry.isEmpty())
			continue;
		if (disabled.contains(e.entry))
			e.enabled = false;
		out << e;
	}
	return out;
}

void chatEntriesToJson(const QList<ChatEntry> &entries, QJsonObject &config)
{
	QJsonArray chats, disabled;
	for (const ChatEntry &e : entries) {
		chats.append(e.entry);
		if (!e.enabled)
			disabled.append(e.entry);
	}
	config.insert("chats", chats);
	if (disabled.isEmpty())
		config.remove("disabled");
	else
		config.insert("disabled", disabled);
}

SourcePlan planSources(const QList<ChatEntry> &entries, bool demo)
{
	SourcePlan plan;
	auto add = [&plan](const QString &entry, bool connect) {
		SourceInfo info;
		EntryProblem problem;
		if (!parseEntry(entry, info, problem.code, problem.arg)) {
			plan.errors << problem;
			return;
		}
		if (!connect)
			return;
		for (const SourceInfo &s : plan.sources)
			if (s.key == info.key)
				return;
		plan.sources.push_back(info);
	};
	for (const ChatEntry &e : entries)
		if (!e.entry.trimmed().isEmpty())
			add(e.entry, e.enabled);
	if (demo)
		add(QStringLiteral("demo:demo"), true);
	return plan;
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
			{"starvios", "nova_estelar", "#A78BFA", ""},
			{"starvios", "lucho_star", "#38BDF8", "vip"},
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
