// Starvios: chat público por Supabase Realtime, igual que su web. Solo lectura y anónimo.

#include "platforms.hpp"
#include "message.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <chrono>
#include <deque>

namespace chat {
namespace starvios {

namespace {

// Constantes públicas de la página (no son credenciales de nadie). Si Starvios las cambia, el conector
// las vuelve a leer de su web: ver recoverCredentials.
const char *const kPublishableKey = "sb_publishable_4lF1QyQhWOUE1Pw3rWBwEw_bVmkAxm6";
const char *const kProjectUrl = "https://naiijcvngtkkocligwvk.supabase.co";
const char *const kSiteUrl = "https://starvios.com";
// Las mismas columnas que pide la página.
const char *const kRowColumns = "id,user_id,body,coins,kind,meta,badges,pinned_until,is_deleted,created_at,"
				"profiles:profiles!live_chat_messages_user_id_fkey(username,display_name,is_partner,"
				"is_verified,is_team,chat_color)";

constexpr int kRestTimeoutMs = 8000;
constexpr int kEmoteTimeoutMs = 20000;
constexpr qint64 kRecoveryCooldownMs = 10 * 60 * 1000;
constexpr qint64 kEmoteRetryMs = 10 * 60 * 1000;
constexpr qint64 kEmoteRefreshMs = 60 * 60 * 1000;
constexpr qint64 kUnknownLogIntervalMs = 5 * 60 * 1000;
constexpr int kMaxEmotes = 20000;
constexpr int kGapLimit = 100;
constexpr int kMaxPendingLookups = 16;
constexpr size_t kMaxSeenIds = 4000;
constexpr int kMaxAuthors = 2000;

// Formato de las difusiones binarias según el cliente de Realtime de la página: 1 byte de tipo,
// 3 con las longitudes de topic, evento y metadatos, 1 de codificación y después esos campos.
constexpr char kUserBroadcast = 4;
constexpr char kJsonEncoding = 1;
constexpr qsizetype kBroadcastHeader = 5;

Frame decodeControl(QByteArrayView data)
{
	Frame frame;
	const QJsonDocument doc = QJsonDocument::fromJson(data.toByteArray());
	const QJsonArray fields = doc.array();
	if (fields.size() != 5 || !fields[2].isString() || !fields[3].isString())
		return frame;
	frame.type = Frame::Type::Control;
	frame.ref = fields[1].toVariant().toString();
	frame.topic = fields[2].toString();
	frame.event = fields[3].toString();
	frame.payload = fields[4].toObject();
	// El cliente de la página también admite las difusiones en texto. Son lo mismo que las binarias:
	// {"event": "msg", "payload": {...}}.
	const QJsonValue event = frame.payload.value(QStringLiteral("event"));
	const QJsonValue content = frame.payload.value(QStringLiteral("payload"));
	if (frame.event == QStringLiteral("broadcast") && event.isString() && content.isObject()) {
		frame.type = Frame::Type::Broadcast;
		frame.event = event.toString();
		frame.payload = content.toObject();
	}
	return frame;
}

Frame decodeBroadcast(QByteArrayView data)
{
	Frame frame;
	if (data.size() < kBroadcastHeader)
		return frame;
	const auto length = [&](qsizetype index) { return qsizetype(static_cast<unsigned char>(data[index])); };
	const qsizetype topicSize = length(1);
	const qsizetype eventSize = length(2);
	const qsizetype metaSize = length(3);
	const qsizetype payloadStart = kBroadcastHeader + topicSize + eventSize + metaSize;
	if (data.size() <= payloadStart)
		return frame;

	frame.topic = QString::fromUtf8(data.sliced(kBroadcastHeader, topicSize));
	frame.event = QString::fromUtf8(data.sliced(kBroadcastHeader + topicSize, eventSize));
	if (data[4] != kJsonEncoding) {
		frame.type = Frame::Type::Ignored;
		return frame;
	}
	const QJsonDocument doc = QJsonDocument::fromJson(data.sliced(payloadStart).toByteArray());
	if (!doc.isObject())
		return frame;
	frame.type = Frame::Type::Broadcast;
	frame.payload = doc.object();
	return frame;
}

} // namespace

Frame decodeFrame(QByteArrayView data)
{
	if (data.isEmpty())
		return {};
	// Un JSON de Phoenix siempre empieza por "["; una difusión binaria, por su tipo (byte pequeño).
	if (data[0] == '[')
		return decodeControl(data);
	if (data[0] == kUserBroadcast)
		return decodeBroadcast(data);
	if (static_cast<unsigned char>(data[0]) < 0x20) {
		Frame other;
		other.type = Frame::Type::Ignored; // otros tipos binarios del protocolo
		return other;
	}
	return {};
}

namespace {

QString jsonText(const QJsonValue &value)
{
	return value.isString() ? value.toString() : QString();
}

bool validPath(const QString &path)
{
	static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_ ./-]{0,255}$"));
	return valid.match(path).hasMatch() && !path.contains(QStringLiteral(".."));
}

// Misma URL que construye la página para las imágenes guardadas (emotes y fotos).
QString imageUrl(const QString &path, int width = 0)
{
	QString url = QStringLiteral("%1/api/public/img?p=%2")
			      .arg(QLatin1String(kSiteUrl), QString::fromLatin1(QUrl::toPercentEncoding(path)));
	if (width > 0)
		url += QStringLiteral("&w=%1&sq=1").arg(width);
	return url;
}

QString formatCoins(double coins)
{
	const qint64 whole = qint64(coins);
	return QStringLiteral("%1 %2").arg(whole).arg(whole == 1 ? QStringLiteral("Starvie") : QStringLiteral("Starvies"));
}

// Insignias de la plataforma → las del modelo común. El resto (donor, hytale...) no tienen equivalente.
QStringList mapBadges(const QJsonObject &row, const QJsonObject &profile, const QString &ownerId)
{
	QStringList out;
	const auto add = [&out](const QString &name) {
		if (!out.contains(name))
			out << name;
	};
	const QJsonArray badges = row.value(QStringLiteral("badges")).toArray();
	for (const QJsonValue value : badges) {
		const QString badge = value.toString();
		if (badge == QStringLiteral("owner") || badge == QStringLiteral("moderator") ||
		    badge == QStringLiteral("vip") || badge == QStringLiteral("staff"))
			add(badge);
		else if (badge == QStringLiteral("member"))
			add(QStringLiteral("subscriber"));
		else if (badge == QStringLiteral("admin"))
			add(QStringLiteral("staff"));
		else if (badge == QStringLiteral("partner"))
			add(QStringLiteral("verified"));
	}
	if (badges.isEmpty() && !ownerId.isEmpty() && jsonText(row.value(QStringLiteral("user_id"))) == ownerId)
		add(QStringLiteral("owner"));
	if (profile.value(QStringLiteral("is_verified")).toBool() || profile.value(QStringLiteral("is_partner")).toBool())
		add(QStringLiteral("verified"));
	if (profile.value(QStringLiteral("is_team")).toBool())
		add(QStringLiteral("staff"));
	return out;
}

// Estado de todo el proceso: la clave vale para todos los canales y el catálogo de emotes pesa 1,5 MB.
struct Shared {
	std::mutex mutex;
	QString key = QString::fromLatin1(kPublishableKey);
	QString projectUrl = QString::fromLatin1(kProjectUrl);
	EmoteMap emotes;
	qint64 nextEmoteFetch = 0;

	std::mutex recoveryMutex; // una sola recuperación a la vez
	qint64 lastRecovery = 0;
};

Shared &shared()
{
	static Shared state;
	return state;
}

struct Credentials {
	QString key;
	QString projectUrl;
};

Credentials credentials()
{
	Shared &s = shared();
	std::lock_guard<std::mutex> lock(s.mutex);
	return {s.key, s.projectUrl};
}

qint64 nowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

// Las claves no deben acabar en el registro ni en la ventana de chats.
QString scrubbed(QString text)
{
	static const QRegularExpression key(QStringLiteral("sb_[A-Za-z0-9_-]+"));
	return text.replace(key, QStringLiteral("<clave>"));
}

void warn(const QString &text)
{
	qWarning("[multichat] Starvios: %s", qUtf8Printable(scrubbed(text)));
}

// Starvios puede cambiar su clave pública, como hizo Kick con la suya: se vuelve a leer de su web
// lo mismo que usa la página. Devuelve true si ya hay otra clave que probar. Como mucho una
// consulta cada kRecoveryCooldownMs, para no insistir si el problema es otro.
bool recoverCredentials(const QString &rejectedKey)
{
	Shared &s = shared();
	std::lock_guard<std::mutex> recovery(s.recoveryMutex);
	if (credentials().key != rejectedKey)
		return true; // otro hilo ya la cambió
	const qint64 now = nowMs();
	if (s.lastRecovery && now - s.lastRecovery < kRecoveryCooldownMs)
		return false;
	s.lastRecovery = now;

	QString key, projectUrl;
	const net::Response home = net::get(std::string(kSiteUrl) + "/", {{"Accept", "text/html"}});
	const QString script = home.ok() ? clientScriptFromHtml(QString::fromStdString(home.body)) : QString();
	if (!script.isEmpty()) {
		const net::Response client = net::get(std::string(kSiteUrl) + script.toStdString());
		if (client.ok())
			credentialsFromScript(QString::fromStdString(client.body), key, projectUrl);
	}
	if (key.isEmpty()) {
		warn(QStringLiteral("rechazó la clave y no se pudo leer una nueva de su web; puede que haya que "
				    "actualizar el plugin"));
		return false;
	}
	if (key == rejectedKey) {
		warn(QStringLiteral("rechazó la clave, pero su web sigue usando la misma"));
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		s.key = key;
		s.projectUrl = projectUrl;
	}
	warn(QStringLiteral("la clave pública cambió; se usará la de su web"));
	return true;
}

// Consulta a la API pública del proyecto, con un reintento si la clave ya no vale.
net::Response rest(const QString &pathAndQuery, const QByteArray *postBody = nullptr)
{
	const auto send = [&](const Credentials &c) {
		const std::string url = (c.projectUrl + pathAndQuery).toStdString();
		const net::Headers headers = {{"apikey", c.key.toStdString()}, {"Accept", "application/json"}};
		return postBody ? net::postJson(url, postBody->toStdString(), headers, kRestTimeoutMs)
				: net::get(url, headers, kRestTimeoutMs);
	};
	const Credentials first = credentials();
	net::Response res = send(first);
	if ((res.status == 401 || res.status == 403) && recoverCredentials(first.key))
		res = send(credentials());
	return res;
}

QString encoded(const QString &value)
{
	return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

QString rowQuery(const QString &filters)
{
	return QStringLiteral("/rest/v1/live_chat_messages?select=%1&%2")
		.arg(QString::fromLatin1(QUrl::toPercentEncoding(QLatin1String(kRowColumns), "(),:!_")), filters);
}

LiveResult fetchDiscovery(const QString &channel, QString &streamId, QString &ownerId, QString &error,
			  Profile *profile = nullptr)
{
	const net::Response res =
		net::get(QStringLiteral("%1/api/public/discovery?kind=channel&username=%2")
				 .arg(QLatin1String(kSiteUrl), encoded(channel))
				 .toStdString(),
			 {{"Accept", "application/json"}}, kRestTimeoutMs);
	if (!res.ok()) {
		error = res.status ? QStringLiteral("HTTP %1").arg(res.status) : QString::fromStdString(res.error);
		return {};
	}
	const QJsonObject json = QJsonDocument::fromJson(QByteArray::fromStdString(res.body)).object();
	if (profile)
		*profile = profileFromDiscovery(json);
	const LiveResult result = parseDiscovery(json, streamId, ownerId);
	if (result.live == -1)
		error = QStringLiteral("respuesta inesperada");
	return result;
}

QByteArray compactJson(const QJsonObject &object)
{
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString joinFrame(const QString &topic)
{
	// Sin presencia: entrar en ella contaría al plugin como un espectador.
	const QJsonObject config{
		{"broadcast", QJsonObject{{"ack", false}, {"self", false}}},
		{"presence", QJsonObject{{"key", ""}, {"enabled", false}}},
		{"postgres_changes", QJsonArray{}},
		{"private", false},
	};
	const QJsonArray frame{"1", "1", topic, "phx_join", QJsonObject{{"config", config}}};
	return QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact));
}

// Eventos que pueden llegar al canal del directo sin que signifique nada nuevo: los del protocolo de
// Realtime y las difusiones de la página que no son mensajes del chat (live_row, ping, raid y
// collaboration, según sus suscripciones en el bundle).
bool isExpectedEvent(const QString &event)
{
	static const QSet<QString> expected{
		QStringLiteral("phx_reply"),        QStringLiteral("phx_join"),      QStringLiteral("phx_leave"),
		QStringLiteral("system"),           QStringLiteral("presence_state"), QStringLiteral("presence_diff"),
		QStringLiteral("postgres_changes"), QStringLiteral("access_token"),  QStringLiteral("live_row"),
		QStringLiteral("ping"),             QStringLiteral("raid"),          QStringLiteral("collaboration"),
	};
	return expected.contains(event);
}

const QRegularExpression &emoteCodePattern()
{
	static const QRegularExpression re(QStringLiteral(":([a-zA-Z0-9_]{2,20}):"));
	return re;
}

} // namespace

EmoteMap parseEmoteCatalog(const QJsonObject &response)
{
	static const QRegularExpression validCode(QStringLiteral("^[a-zA-Z0-9_]{2,20}$"));
	EmoteMap out;
	const QJsonArray rows = response.value(QStringLiteral("rows")).toArray();
	for (const QJsonValue value : rows) {
		if (out.size() >= kMaxEmotes)
			break;
		const QJsonObject row = value.toObject();
		const QString code = jsonText(row.value(QStringLiteral("code")));
		const QString path = jsonText(row.value(QStringLiteral("storage_path")));
		if (validCode.match(code).hasMatch() && validPath(path))
			out.insert(code.toLower(), imageUrl(path));
	}
	return out;
}

QJsonArray splitBody(const QString &body, const EmoteMap &emotes)
{
	// Los enlaces van primero en la alternativa para que un :código: dentro de una URL no cuente.
	static const QRegularExpression token(
		QStringLiteral("(?:https?://|www\\.)\\S+|:([a-zA-Z0-9_]{2,20}):"),
		QRegularExpression::CaseInsensitiveOption);
	QJsonArray parts;
	QString text;
	qsizetype cursor = 0;
	auto it = token.globalMatch(body);
	while (it.hasNext()) {
		const QRegularExpressionMatch match = it.next();
		const auto emote = emotes.constFind(match.captured(1).toLower());
		if (match.captured(1).isEmpty() || emote == emotes.constEnd())
			continue;
		text += body.mid(cursor, match.capturedStart() - cursor);
		if (!text.isEmpty())
			parts.append(textPart(text));
		text.clear();
		parts.append(emotePart(*emote, match.captured()));
		cursor = match.capturedEnd();
	}
	text += body.mid(cursor);
	if (!text.isEmpty())
		parts.append(textPart(text));
	return parts;
}

QString authorOf(const QJsonObject &row)
{
	const QJsonObject profile = row.value(QStringLiteral("profiles")).toObject();
	const QString display = jsonText(profile.value(QStringLiteral("display_name"))).trimmed();
	return display.isEmpty() ? jsonText(profile.value(QStringLiteral("username"))).trimmed() : display;
}

QJsonObject toMessage(const QJsonObject &row, const QString &channel, const QString &ownerId, const EmoteMap &emotes,
		      bool recovered)
{
	const QString id = jsonText(row.value(QStringLiteral("id")));
	if (id.isEmpty() || row.value(QStringLiteral("is_deleted")).toBool())
		return {};
	const QJsonObject profile = row.value(QStringLiteral("profiles")).toObject();
	const QJsonObject meta = row.value(QStringLiteral("meta")).toObject();
	const QString kind = jsonText(row.value(QStringLiteral("kind")));
	const QString body = jsonText(row.value(QStringLiteral("body")));
	const double coins = row.value(QStringLiteral("coins")).toVariant().toDouble();
	const auto metaText = [&meta](const char *key) { return jsonText(meta.value(QLatin1String(key))).trimmed(); };
	const auto metaInt = [&meta](const char *key, int fallback) {
		const QJsonValue value = meta.value(QLatin1String(key));
		return value.isDouble() || value.isString() ? value.toVariant().toInt() : fallback;
	};

	QString author = authorOf(row);
	if (author.isEmpty())
		author = QStringLiteral("Espectador");

	MessageOptions o;
	o.id = id;
	o.recovered = recovered;
	o.badges = mapBadges(row, profile, ownerId);
	static const QRegularExpression validColor(QStringLiteral("^#[0-9a-fA-F]{6}$"));
	const QString color = jsonText(profile.value(QStringLiteral("chat_color")));
	if (validColor.match(color).hasMatch())
		o.color = color;

	// Mismos tipos que distingue la página. Cualquier otro se muestra como un mensaje normal.
	bool showBody = true;
	if (kind == QStringLiteral("raid")) {
		const int viewers = metaInt("viewers", 0);
		// arg() con varios argumentos sustituye a la vez: un "%2" en el nombre queda como texto.
		o.system = QStringLiteral("%1 está haciendo una raid con %2 %3")
				   .arg(author, QString::number(viewers),
					viewers == 1 ? QStringLiteral("espectador") : QStringLiteral("espectadores"));
		showBody = false;
	} else if (kind == QStringLiteral("reward")) {
		const QString title = metaText("reward_title");
		const QString emoji = metaText("emoji");
		o.system = QStringLiteral("%1 canjeó %2%3")
				   .arg(author, emoji.isEmpty() ? QString() : emoji + QLatin1Char(' '),
					title.isEmpty() ? QStringLiteral("una recompensa") : title);
		if (metaInt("cost", 0) > 0)
			o.amount = QString::number(metaInt("cost", 0));
		showBody = meta.value(QStringLiteral("has_input")).toBool();
	} else if (kind == QStringLiteral("sub")) {
		const int months = metaInt("months", 1);
		o.system = months > 1 ? QStringLiteral("%1 se ha suscrito (%2 meses)").arg(author, QString::number(months))
				      : QStringLiteral("%1 se ha suscrito").arg(author);
		showBody = false;
	} else if (kind == QStringLiteral("gift_sub")) {
		if (meta.value(QStringLiteral("anonymous")).toBool())
			author = QStringLiteral("Anónimo");
		const QString recipient = metaText("recipient");
		o.system = recipient.isEmpty() ? QStringLiteral("%1 regaló una suscripción").arg(author)
					       : QStringLiteral("%1 regaló una suscripción a %2").arg(author, recipient);
		showBody = false;
	} else if (kind == QStringLiteral("starfall")) {
		o.system = body.trimmed();
		showBody = false;
	} else if (kind == QStringLiteral("pumpkin_win")) {
		o.system = QStringLiteral("¡Enhorabuena, %1! Ha cazado 10 calabazas y ha recibido 100 Starvies").arg(author);
		showBody = false;
	}
	if (!o.system.isEmpty())
		o.kind = QStringLiteral("highlight");
	if (coins > 0) {
		o.kind = QStringLiteral("highlight");
		if (o.amount.isEmpty())
			o.amount = formatCoins(coins);
	}

	const QJsonArray parts = showBody && !body.trimmed().isEmpty() ? splitBody(body, emotes) : QJsonArray();
	if (parts.isEmpty() && o.system.isEmpty() && coins <= 0)
		return {};
	return makeMessage(QStringLiteral("starvios"), channel, author, parts, o);
}

LiveResult parseDiscovery(const QJsonObject &response, QString &streamId, QString &channelId)
{
	streamId.clear();
	channelId.clear();
	LiveResult result;
	const QJsonValue channel = response.value(QStringLiteral("channel"));
	if (!channel.isObject())
		return result;
	result.live = 0;
	const QJsonObject stream = response.value(QStringLiteral("stream")).toObject();
	if (jsonText(stream.value(QStringLiteral("status"))) != QStringLiteral("live"))
		return result;
	// El id acaba en un topic y en URLs: si no tiene la forma esperada, mejor no usarlo.
	static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9-]{1,64}$"));
	const QString id = jsonText(stream.value(QStringLiteral("id")));
	if (!validId.match(id).hasMatch()) {
		result.live = -1;
		return result;
	}
	result.live = 1;
	streamId = id;
	channelId = jsonText(channel.toObject().value(QStringLiteral("id")));
	return result;
}

Profile profileFromDiscovery(const QJsonObject &response)
{
	const QJsonObject channel = response.value(QStringLiteral("channel")).toObject();
	Profile profile;
	profile.name = jsonText(channel.value(QStringLiteral("display_name")));
	if (profile.name.isEmpty())
		profile.name = jsonText(channel.value(QStringLiteral("username")));
	const QString avatar = jsonText(channel.value(QStringLiteral("avatar_url")));
	if (avatar.startsWith(QStringLiteral("https://")))
		profile.avatar = avatar;
	else if (validPath(avatar))
		profile.avatar = imageUrl(avatar, 128);
	return profile;
}

int viewersFromCounts(const QJsonArray &rows, const QString &streamId)
{
	for (const QJsonValue value : rows) {
		const QJsonObject row = value.toObject();
		if (jsonText(row.value(QStringLiteral("stream_id"))) == streamId)
			return row.value(QStringLiteral("viewers")).toInt(-1);
	}
	return -1;
}

QString clientScriptFromHtml(const QString &html)
{
	// Ruta local: lo que hay delante no puede ser parte de una URL a otro sitio.
	static const QRegularExpression re(QStringLiteral("(?<![A-Za-z0-9_./:-])/assets/client-[A-Za-z0-9_-]+\\.js"));
	return re.match(html).captured();
}

bool credentialsFromScript(const QString &script, QString &key, QString &projectUrl)
{
	static const QRegularExpression keyRe(QStringLiteral("sb_publishable_[A-Za-z0-9_-]{8,128}"));
	// Solo un proyecto de Supabase: un valor inesperado no debe mandar la conexión a otro sitio.
	static const QRegularExpression named(
		QStringLiteral("VITE_SUPABASE_URL\\W{1,4}(https://[a-z0-9-]{3,63}\\.supabase\\.co)\\b"));
	static const QRegularExpression anyUrl(QStringLiteral("https://[a-z0-9-]{3,63}\\.supabase\\.co\\b"));
	const QString foundKey = keyRe.match(script).captured();
	QString foundUrl = named.match(script).captured(1);
	if (foundUrl.isEmpty())
		foundUrl = anyUrl.match(script).captured();
	if (foundKey.isEmpty() || foundUrl.isEmpty())
		return false;
	key = foundKey;
	projectUrl = foundUrl;
	return true;
}

LiveResult checkLive(const QString &channel)
{
	QString streamId, ownerId, error;
	LiveResult result = fetchDiscovery(channel, streamId, ownerId, error);
	if (result.live != 1)
		return result;
	const QByteArray body = compactJson(QJsonObject{{"_stream_ids", QJsonArray{streamId}}});
	const net::Response res = rest(QStringLiteral("/rest/v1/rpc/live_viewer_counts"), &body);
	if (res.ok())
		result.viewers = viewersFromCounts(QJsonDocument::fromJson(QByteArray::fromStdString(res.body)).array(),
						   streamId);
	return result;
}

Profile fetchProfile(const QString &channel)
{
	QString streamId, ownerId, error;
	Profile profile;
	fetchDiscovery(channel, streamId, ownerId, error, &profile);
	return profile;
}

class StarviosConnector : public Connector {
public:
	StarviosConnector(SourceInfo source, PostFn post, ReportFn report, Timing timing)
		: Connector(std::move(source), std::move(post), std::move(report)),
		  timing_(timing)
	{
	}

protected:
	void session() override
	{
		QString streamId, ownerId;
		for (;;) {
			QString error;
			const LiveResult live = fetchDiscovery(source_.channel, streamId, ownerId, error);
			if (live.live == 1)
				break;
			if (live.live == -1)
				throw ChatError(QStringLiteral("no se pudo consultar Starvios (%1)").arg(error));
			report_("waiting", QStringLiteral("esperando a que empiece el directo"));
			if (wait(timing_.offlineMs))
				return;
		}
		enterStream(streamId, ownerId);

		const Credentials used = credentials();
		QString socketUrl = used.projectUrl;
		socketUrl.replace(0, 5, QStringLiteral("wss")); // https → wss
		socketUrl += QStringLiteral("/realtime/v1/websocket?apikey=%1&vsn=2.0.0").arg(encoded(used.key));
		std::string connectError;
		auto ws = net::connectWebSocket(socketUrl.toStdString(), {{"Origin", kSiteUrl}}, connectError);
		if (!ws)
			throw ChatError(scrubbed(QStringLiteral("no se pudo conectar: %1")
							 .arg(QString::fromStdString(connectError))));
		setSocket(ws);

		const QString topic = QStringLiteral("realtime:stream:%1").arg(streamId);
		ws->send(joinFrame(topic).toStdString());
		Background background(this, ws, streamId);

		bool joined = false;
		std::string raw;
		while (!stopped()) {
			if (ws->receive(raw) == net::WebSocket::Result::Closed) {
				if (stopped() || background.streamChanged())
					return;
				if (joined)
					throw ChatError("Starvios cerró la conexión");
				recoverCredentials(used.key);
				throw ChatError("Starvios no aceptó la conexión (puede que haya cambiado algo)");
			}
			const Frame frame = decodeFrame(QByteArrayView(raw.data(), qsizetype(raw.size())));
			if (frame.type == Frame::Type::Invalid) {
				logUnknown(QStringLiteral("trama no reconocida de %1 bytes").arg(raw.size()));
			} else if (frame.type == Frame::Type::Broadcast) {
				if (frame.event == QStringLiteral("msg"))
					onBroadcast(frame.payload, background);
				else if (frame.topic == topic)
					logUnknownEvent(frame.event);
			} else if (frame.type == Frame::Type::Control) {
				if (frame.event == QStringLiteral("phx_reply") && frame.topic == QStringLiteral("phoenix")) {
					background.heartbeatAnswered(frame.ref);
				} else if (frame.topic != topic) {
					continue;
				} else if (frame.event == QStringLiteral("phx_reply") && frame.ref == QStringLiteral("1") && !joined) {
					const QJsonObject response = frame.payload.value(QStringLiteral("response")).toObject();
					if (jsonText(frame.payload.value(QStringLiteral("status"))) != QStringLiteral("ok")) {
						recoverCredentials(used.key);
						throw ChatError(scrubbed(QStringLiteral("Starvios rechazó la conexión (%1)")
										 .arg(jsonText(response.value(QStringLiteral("reason"))).left(120))));
					}
					joined = true;
					background.joined();
					report_("connected", QString());
					recoverGap(background, streamId);
				} else if (frame.event == QStringLiteral("phx_error") || frame.event == QStringLiteral("phx_close")) {
					throw ChatError("Starvios cerró el canal del chat");
				} else {
					logUnknownEvent(frame.event);
				}
			}
		}
	}

private:
	// Trabajo que no puede bloquear la lectura del socket: el latido en un hilo y las consultas de red
	// en otro, para que una consulta lenta no retrase el latido.
	class Background {
	public:
		Background(StarviosConnector *owner, std::shared_ptr<net::WebSocket> ws, const QString &streamId)
			: owner_(owner),
			  ws_(std::move(ws)),
			  streamId_(streamId),
			  heartbeat_([this] { heartbeatLoop(); }),
			  fetcher_([this] { fetchLoop(); })
		{
		}
		~Background()
		{
			{
				std::lock_guard<std::mutex> lock(mutex_);
				done_ = true;
			}
			cv_.notify_all();
			heartbeat_.join();
			fetcher_.join();
		}

		void joined()
		{
			{
				std::lock_guard<std::mutex> lock(mutex_);
				joined_ = true;
			}
			cv_.notify_all();
		}
		void heartbeatAnswered(const QString &ref)
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (ref == pendingBeat_)
				pendingBeat_.clear();
		}
		// discard se llama en lugar de task si la sesión acaba antes de que le llegue el turno.
		void enqueue(std::function<void()> task, std::function<void()> discard = {})
		{
			{
				std::lock_guard<std::mutex> lock(mutex_);
				tasks_.push_back({std::move(task), std::move(discard)});
			}
			cv_.notify_all();
		}
		bool streamChanged() const { return streamChanged_; }

	private:
		struct Task {
			std::function<void()> run;
			std::function<void()> discard;
		};

		void heartbeatLoop()
		{
			std::unique_lock<std::mutex> lock(mutex_);
			cv_.wait_for(lock, std::chrono::milliseconds(owner_->timing_.joinTimeoutMs),
				     [this] { return done_ || joined_; });
			if (done_)
				return;
			if (!joined_) {
				lock.unlock();
				ws_->close();
				return;
			}
			quint64 ref = 1; // el "1" es el phx_join
			while (!done_) {
				if (cv_.wait_for(lock, std::chrono::milliseconds(owner_->timing_.heartbeatMs),
						 [this] { return done_; }))
					return;
				// Si el latido anterior sigue sin respuesta, la conexión está muerta aunque no lo parezca.
				if (!pendingBeat_.isEmpty()) {
					lock.unlock();
					ws_->close();
					return;
				}
				pendingBeat_ = QString::number(++ref);
				const QJsonArray beat{QJsonValue::Null, pendingBeat_, "phoenix", "heartbeat", QJsonObject{}};
				const std::string text = QJsonDocument(beat).toJson(QJsonDocument::Compact).toStdString();
				lock.unlock();
				const bool sent = ws_->send(text);
				lock.lock();
				if (!sent)
					return;
			}
		}

		void fetchLoop()
		{
			auto nextCheck = std::chrono::steady_clock::now() + std::chrono::milliseconds(owner_->timing_.discoveryMs);
			std::unique_lock<std::mutex> lock(mutex_);
			while (!done_) {
				cv_.wait_until(lock, nextCheck, [this] { return done_ || !tasks_.empty(); });
				if (done_)
					break;
				if (!tasks_.empty()) {
					Task task = std::move(tasks_.front());
					tasks_.pop_front();
					lock.unlock();
					task.run();
					lock.lock();
					continue;
				}
				lock.unlock();
				checkStream();
				lock.lock();
				nextCheck = std::chrono::steady_clock::now() + std::chrono::milliseconds(owner_->timing_.discoveryMs);
			}
			// La sesión acaba: nada de red aquí, porque retrasaría la reconexión o el cierre.
			std::deque<Task> discarded;
			discarded.swap(tasks_);
			lock.unlock();
			for (const Task &task : discarded)
				if (task.discard)
					task.discard();
		}

		// Un directo nuevo tiene otro id: se cierra este socket y la sesión siguiente entra en el nuevo.
		void checkStream()
		{
			QString streamId, ownerId, error;
			const LiveResult live = fetchDiscovery(owner_->source_.channel, streamId, ownerId, error);
			if (live.live == 0 || (live.live == 1 && streamId != streamId_)) {
				streamChanged_ = true;
				ws_->close();
			}
		}

		StarviosConnector *owner_;
		std::shared_ptr<net::WebSocket> ws_;
		const QString streamId_;
		std::mutex mutex_;
		std::condition_variable cv_;
		std::deque<Task> tasks_;
		QString pendingBeat_;
		bool done_ = false;
		bool joined_ = false;
		std::atomic<bool> streamChanged_{false};
		std::thread heartbeat_; // los hilos los últimos: arrancan cuando lo demás ya existe
		std::thread fetcher_;
	};

	void enterStream(const QString &streamId, const QString &ownerId)
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		ownerId_ = ownerId;
		if (streamId == streamId_)
			return;
		streamId_ = streamId;
		lastCreatedAt_.clear();
		lastCreatedMs_ = 0;
		seen_.clear();
		seenOrder_.clear();
	}

	// Formas desconocidas: una línea de vez en cuando con los nombres de los campos, nunca el contenido.
	void logUnknown(const QString &what)
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		const qint64 now = nowMs();
		if (lastUnknownLog_ && now - lastUnknownLog_ < kUnknownLogIntervalMs)
			return;
		lastUnknownLog_ = now;
		warn(what);
	}

	void logUnknownEvent(QString event)
	{
		if (isExpectedEvent(event))
			return;
		// El nombre viene de fuera: solo caracteres que no puedan falsear una línea del registro.
		static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9_.:-]"));
		logUnknown(QStringLiteral("evento desconocido en el canal: %1").arg(event.remove(unsafe).left(40)));
	}

	void onBroadcast(const QJsonObject &payload, Background &background)
	{
		const QString op = jsonText(payload.value(QStringLiteral("op")));
		const QJsonObject row = payload.value(QStringLiteral("msg")).toObject();
		if (jsonText(row.value(QStringLiteral("id"))).isEmpty()) {
			QStringList keys = (row.isEmpty() ? payload : row).keys();
			logUnknown(QStringLiteral("mensaje con una forma desconocida (campos: %1)").arg(keys.mid(0, 16).join(',').left(200)));
			return;
		}
		// Una edición o un borrado de un mensaje ya emitido no es un mensaje nuevo.
		if (op == QStringLiteral("UPDATE") || op == QStringLiteral("DELETE"))
			return;
		if (row.value(QStringLiteral("is_deleted")).toBool() || !remember(jsonText(row.value(QStringLiteral("id")))))
			return;
		noteCreatedAt(jsonText(row.value(QStringLiteral("created_at"))));
		loadEmotesIfNeeded(jsonText(row.value(QStringLiteral("body"))), background);

		if (!authorOf(row).isEmpty()) {
			rememberAuthor(row);
			publish(row, false);
			return;
		}
		QJsonObject withAuthor = row;
		const QString userId = jsonText(row.value(QStringLiteral("user_id")));
		{
			std::lock_guard<std::mutex> lock(stateMutex_);
			const auto known = authors_.constFind(userId);
			if (known != authors_.constEnd())
				withAuthor.insert(QStringLiteral("profiles"), *known);
		}
		if (!authorOf(withAuthor).isEmpty()) {
			publish(withAuthor, false);
			return;
		}
		// Sin perfil: se pide esta fila una vez, salvo que ya haya demasiadas en cola.
		if (pendingLookups_ >= kMaxPendingLookups) {
			publish(row, false);
			return;
		}
		++pendingLookups_;
		background.enqueue(
			[this, row] {
				lookUpAuthor(row);
				--pendingLookups_;
			},
			// Ya está recordada y el hueco no la devolverá: se muestra con la etiqueta neutra.
			[this, row] {
				publish(row, false);
				--pendingLookups_;
			});
	}

	void lookUpAuthor(const QJsonObject &row)
	{
		const net::Response res = rest(rowQuery(
			QStringLiteral("id=eq.%1&limit=1").arg(encoded(jsonText(row.value(QStringLiteral("id")))))));
		const QJsonObject found =
			res.ok() ? QJsonDocument::fromJson(QByteArray::fromStdString(res.body)).array().at(0).toObject() : QJsonObject();
		if (found.isEmpty()) {
			publish(row, false);
			return;
		}
		rememberAuthor(found);
		publish(found, false);
	}

	// Al reconectar en el mismo directo se muestra lo que se perdió, marcado para que no se lea en voz alta.
	void recoverGap(Background &background, const QString &streamId)
	{
		QString last;
		{
			std::lock_guard<std::mutex> lock(stateMutex_);
			last = lastCreatedAt_;
		}
		if (last.isEmpty())
			return; // primera conexión: el historial nunca se muestra como nuevo
		background.enqueue([this, streamId, last] {
			const net::Response res = rest(
				rowQuery(QStringLiteral("stream_id=eq.%1&created_at=gt.%2&order=created_at.asc&limit=%3")
						 .arg(encoded(streamId), encoded(last), QString::number(kGapLimit))));
			if (!res.ok())
				return;
			const QJsonArray rows = QJsonDocument::fromJson(QByteArray::fromStdString(res.body)).array();
			for (const QJsonValue value : rows) {
				const QJsonObject row = value.toObject();
				if (row.value(QStringLiteral("is_deleted")).toBool() ||
				    !remember(jsonText(row.value(QStringLiteral("id")))))
					continue;
				noteCreatedAt(jsonText(row.value(QStringLiteral("created_at"))));
				rememberAuthor(row);
				publish(row, true);
			}
		});
	}

	void publish(const QJsonObject &row, bool recovered)
	{
		// Una consulta en segundo plano puede terminar cuando el chat ya se ha quitado.
		if (stopped())
			return;
		QString ownerId;
		{
			std::lock_guard<std::mutex> lock(stateMutex_);
			ownerId = ownerId_;
		}
		EmoteMap emotes;
		{
			Shared &s = shared();
			std::lock_guard<std::mutex> lock(s.mutex);
			emotes = s.emotes;
		}
		const QJsonObject message = toMessage(row, source_.channel, ownerId, emotes, recovered);
		if (!message.isEmpty())
			post_(message);
	}

	// El catálogo pesa 1,5 MB: solo se pide al ver un :código: que no se conoce, una vez para todo el
	// proceso y en segundo plano. Hasta que llega, el código se ve como texto.
	void loadEmotesIfNeeded(const QString &body, Background &background)
	{
		if (!body.contains(QLatin1Char(':')))
			return;
		Shared &s = shared();
		std::lock_guard<std::mutex> lock(s.mutex);
		const qint64 now = nowMs();
		if (now < s.nextEmoteFetch)
			return;
		bool unknown = false;
		auto it = emoteCodePattern().globalMatch(body);
		while (!unknown && it.hasNext())
			unknown = !s.emotes.contains(it.next().captured(1).toLower());
		if (!unknown)
			return;
		s.nextEmoteFetch = now + kEmoteRetryMs;
		background.enqueue([] {
			const net::Response res = net::get(std::string(kSiteUrl) + "/api/public/emotes",
							   {{"Accept", "application/json"}}, kEmoteTimeoutMs);
			const EmoteMap emotes =
				res.ok() ? parseEmoteCatalog(QJsonDocument::fromJson(QByteArray::fromStdString(res.body)).object())
					 : EmoteMap();
			if (emotes.isEmpty()) {
				warn(QStringLiteral("no se pudo cargar la lista de emotes"));
				return;
			}
			Shared &state = shared();
			std::lock_guard<std::mutex> guard(state.mutex);
			state.emotes = emotes;
			state.nextEmoteFetch = nowMs() + kEmoteRefreshMs;
		});
	}

	bool remember(const QString &id)
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (seen_.contains(id))
			return false;
		seen_.insert(id);
		seenOrder_.push_back(id);
		if (seenOrder_.size() > kMaxSeenIds) {
			seen_.remove(seenOrder_.front());
			seenOrder_.pop_front();
		}
		return true;
	}

	void rememberAuthor(const QJsonObject &row)
	{
		const QString userId = jsonText(row.value(QStringLiteral("user_id")));
		if (userId.isEmpty() || authorOf(row).isEmpty())
			return;
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (authors_.size() >= kMaxAuthors)
			authors_.clear();
		authors_.insert(userId, row.value(QStringLiteral("profiles")).toObject());
	}

	void noteCreatedAt(const QString &createdAt)
	{
		static const QRegularExpression valid(QStringLiteral("^[0-9TZ:.+-]{10,40}$"));
		if (!valid.match(createdAt).hasMatch())
			return;
		const QDateTime time = QDateTime::fromString(createdAt, Qt::ISODateWithMs);
		if (!time.isValid())
			return;
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (time.toMSecsSinceEpoch() >= lastCreatedMs_) {
			lastCreatedMs_ = time.toMSecsSinceEpoch();
			lastCreatedAt_ = createdAt;
		}
	}

	const Timing timing_;
	std::mutex stateMutex_;
	QString streamId_;
	QString ownerId_;
	QString lastCreatedAt_;
	qint64 lastCreatedMs_ = 0;
	QSet<QString> seen_;
	std::deque<QString> seenOrder_;
	QHash<QString, QJsonObject> authors_;
	qint64 lastUnknownLog_ = 0;
	std::atomic<int> pendingLookups_{0};
};

std::shared_ptr<Connector> create(const SourceInfo &s, Connector::PostFn post, Connector::ReportFn report, Timing timing)
{
	return std::make_shared<StarviosConnector>(s, std::move(post), std::move(report), timing);
}

void resetSharedState()
{
	Shared &s = shared();
	std::lock_guard<std::mutex> recovery(s.recoveryMutex);
	std::lock_guard<std::mutex> lock(s.mutex);
	s.key = QString::fromLatin1(kPublishableKey);
	s.projectUrl = QString::fromLatin1(kProjectUrl);
	s.emotes.clear();
	s.nextEmoteFetch = 0;
	s.lastRecovery = 0;
}

} // namespace starvios
} // namespace chat
