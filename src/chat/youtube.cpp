// YouTube: lee el chat público del directo con el endpoint interno de la web (sin API key).

#include "platforms.hpp"
#include "message.hpp"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace chat {
namespace youtube {

namespace {

// Evita la página de consentimiento de cookies en la UE.
const net::Headers kCookies = {{"Cookie", "CONSENT=YES+1; SOCS=CAI"}, {"Accept-Language", "es-ES,es;q=0.9,en;q=0.8"}};

QString channelUrl(const QString &channel)
{
	if (channel.startsWith("UC") && channel.size() == 24)
		return "https://www.youtube.com/channel/" + channel;
	return "https://www.youtube.com/" + (channel.startsWith('@') ? channel : '@' + channel);
}

QString simpleText(const QJsonObject &obj)
{
	if (obj.contains("simpleText"))
		return obj.value("simpleText").toString();
	QString out;
	for (const QJsonValue r : obj.value("runs").toArray())
		out += r.toObject().value("text").toString();
	return out;
}

QJsonArray runsToParts(const QJsonArray &runs)
{
	QJsonArray parts;
	for (const QJsonValue v : runs) {
		const QJsonObject r = v.toObject();
		if (r.contains("text")) {
			parts.append(textPart(r.value("text").toString()));
		} else if (r.contains("emoji")) {
			const QJsonObject e = r.value("emoji").toObject();
			const QJsonArray thumbs = e.value("image").toObject().value("thumbnails").toArray();
			const QJsonArray shortcuts = e.value("shortcuts").toArray();
			const QString name = shortcuts.isEmpty() ? e.value("emojiId").toString()
								 : shortcuts[0].toString();
			if (e.value("isCustomEmoji").toBool() && !thumbs.isEmpty())
				parts.append(emotePart(thumbs.last().toObject().value("url").toString(), name));
			else
				parts.append(textPart(e.value("emojiId").toString(name)));
		}
	}
	return parts;
}

QStringList badges(const QJsonObject &renderer)
{
	QStringList out;
	for (const QJsonValue v : renderer.value("authorBadges").toArray()) {
		const QJsonObject b = v.toObject().value("liveChatAuthorBadgeRenderer").toObject();
		const QString icon = b.value("icon").toObject().value("iconType").toString();
		QString name;
		if (icon == "OWNER")
			name = "owner";
		else if (icon == "MODERATOR")
			name = "moderator";
		else if (icon == "VERIFIED" || icon == "CHECK_CIRCLE_THICK")
			name = "verified";
		else if (b.contains("customThumbnail"))
			name = "member";
		if (!name.isEmpty() && !out.contains(name))
			out << name;
	}
	return out;
}

QString findLiveVideo(const QString &channel, int *viewers = nullptr)
{
	net::Response res = net::get((channelUrl(channel) + "/live").toStdString(), kCookies);
	if (!res.ok())
		throw ChatError(Text{"Error.Responded",
				     {QStringLiteral("YouTube"),
				      res.status ? QString::number(res.status) : QString::fromStdString(res.error)}});
	const QString html = QString::fromUtf8(res.body);
	const QString video = liveVideoFromPage(html);
	if (viewers && !video.isEmpty())
		*viewers = viewersFromPage(html);
	return video;
}

bool videoIsLive(const QString &videoId, int &viewers)
{
	net::Response res = net::get(("https://www.youtube.com/watch?v=" + videoId).toStdString(), kCookies);
	if (!res.ok())
		throw ChatError(Text{"Error.YouTubeVideo", {}});
	const QString html = QString::fromUtf8(res.body);
	if (!html.contains("\"isLiveNow\":true"))
		return false;
	viewers = viewersFromPage(html);
	return true;
}

QString capture(const QString &text, const char *pattern)
{
	QRegularExpressionMatch m = QRegularExpression(QString::fromLatin1(pattern)).match(text);
	return m.hasMatch() ? m.captured(1) : QString();
}

} // namespace

QString parseVideoId(const QString &value)
{
	static const QRegularExpression re(QStringLiteral("(?:v=|youtu\\.be/|/live/|/shorts/)([\\w-]{11})"));
	QRegularExpressionMatch m = re.match(value);
	return m.hasMatch() ? m.captured(1) : QString();
}

QString liveVideoFromPage(const QString &html)
{
	static const QRegularExpression canon(
		QStringLiteral("<link rel=\"canonical\" href=\"https://www\\.youtube\\.com/watch\\?v=([\\w-]{11})\""));
	QRegularExpressionMatch m = canon.match(html);
	if (m.hasMatch() && html.contains("\"isLiveNow\":true"))
		return m.captured(1);
	return {};
}

int viewersFromPage(const QString &html)
{
	// En un directo, videoViewCountRenderer lleva "isLive":true y los que lo ven ahora:
	// "originalViewCount":"1234" o, si falta, el texto "1.234 personas viendo".
	const qsizetype at = html.indexOf(QStringLiteral("\"videoViewCountRenderer\":"));
	if (at < 0)
		return -1;
	const QString block = html.mid(at, 1000);
	if (!block.contains(QStringLiteral("\"isLive\":true")))
		return -1;
	static const QRegularExpression original(QStringLiteral("\"originalViewCount\":\"(\\d+)\""));
	QRegularExpressionMatch m = original.match(block);
	if (m.hasMatch())
		return m.captured(1).toInt();
	static const QRegularExpression text(QStringLiteral("\"(?:text|simpleText)\":\"([\\d.,\\s\u00a0\u202f]+)"));
	m = text.match(block);
	if (!m.hasMatch())
		return -1;
	QString digits;
	for (QChar c : m.captured(1))
		if (c.isDigit())
			digits += c;
	bool ok = false;
	const int n = digits.toInt(&ok);
	return ok ? n : -1;
}

namespace {

// YouTube manda los colores como enteros ARGB (4280191205 = #FF1E88E5).
QString argbToHex(const QJsonValue &value)
{
	const double number = value.isString() ? value.toString().toDouble() : value.toDouble(-1);
	if (number < 0 || number > 4294967295.0)
		return {};
	const quint32 argb = quint32(number);
	return QStringLiteral("#%1").arg(argb & 0xFFFFFF, 6, 16, QLatin1Char('0'));
}

// Segundos que YouTube deja fijado un Super Chat según su nivel (se reconoce por el color del
// cuerpo): verde 2 min, amarillo 5, naranja 10, magenta 30 y rojo 1 h. Azul y celeste no se fijan.
int pinSeconds(const QString &bodyColor)
{
	static const std::pair<const char *, int> tiers[] = {
		{"#1de9b6", 120}, {"#ffca28", 300}, {"#f57c00", 600}, {"#e91e63", 1800}, {"#e62117", 3600},
	};
	for (const auto &[color, seconds] : tiers)
		if (bodyColor.compare(QLatin1String(color), Qt::CaseInsensitive) == 0)
			return seconds;
	return 0;
}

// Colores de un Super Chat o Super Sticker: {header, body, headerText, bodyText, pin}. Vacío si
// YouTube no los manda (membresías, o si cambian el formato): se muestra como hasta ahora.
QJsonObject paidColors(const QJsonObject &r, bool sticker)
{
	const QString body = argbToHex(r.value(sticker ? "backgroundColor" : "bodyBackgroundColor"));
	QString header = argbToHex(r.value(sticker ? "moneyChipBackgroundColor" : "headerBackgroundColor"));
	if (body.isEmpty() && header.isEmpty())
		return {};
	if (header.isEmpty())
		header = body;
	QJsonObject paid{{"header", header}, {"body", body.isEmpty() ? header : body}};
	const QString headerText = argbToHex(r.value(sticker ? "moneyChipTextColor" : "headerTextColor"));
	const QString bodyText = argbToHex(r.value(sticker ? "authorNameTextColor" : "bodyTextColor"));
	if (!headerText.isEmpty())
		paid.insert("headerText", headerText);
	if (!bodyText.isEmpty())
		paid.insert("bodyText", bodyText);
	paid.insert("pin", pinSeconds(paid.value("body").toString()));
	return paid;
}

// La imagen más grande de un Super Sticker (las URL llegan a veces sin "https:").
QString stickerUrl(const QJsonObject &r)
{
	const QJsonArray thumbs = r.value("sticker").toObject().value("thumbnails").toArray();
	if (thumbs.isEmpty())
		return {};
	QString url = thumbs.last().toObject().value("url").toString();
	if (url.startsWith(QLatin1String("//")))
		url.prepend(QLatin1String("https:"));
	return url.startsWith(QLatin1String("https://")) ? url : QString();
}

} // namespace

QJsonObject toMessage(const QJsonObject &action, const QString &channel)
{
	const QJsonObject item = action.value("addChatItemAction").toObject().value("item").toObject();
	static const std::pair<const char *, const char *> kinds[] = {
		{"liveChatTextMessageRenderer", "chat"},
		{"liveChatPaidMessageRenderer", "highlight"},
		{"liveChatMembershipItemRenderer", "highlight"},
		{"liveChatPaidStickerRenderer", "highlight"},
	};
	for (const auto &[key, kind] : kinds) {
		if (!item.contains(key))
			continue;
		const QJsonObject r = item.value(key).toObject();
		MessageOptions o;
		o.kind = kind;
		o.badges = badges(r);
		o.amount = simpleText(r.value("purchaseAmountText").toObject());
		o.id = r.value("id").toString();
		o.authorId = r.value("authorExternalChannelId").toString();
		if (QString::fromLatin1(key) == "liveChatMembershipItemRenderer")
			o.system = simpleText(r.value("headerSubtext").toObject());
		QString author = simpleText(r.value("authorName").toObject());
		QJsonObject message = makeMessage("youtube", channel, author.isEmpty() ? QStringLiteral("?") : author,
						  runsToParts(r.value("message").toObject().value("runs").toArray()), o);
		const QString type = QString::fromLatin1(key);
		const bool sticker = type == "liveChatPaidStickerRenderer";
		if (sticker || type == "liveChatPaidMessageRenderer") {
			const QJsonObject paid = paidColors(r, sticker);
			if (!paid.isEmpty())
				message.insert("paid", paid);
			const QString image = sticker ? stickerUrl(r) : QString();
			if (!image.isEmpty())
				message.insert("sticker", image);
		}
		return message;
	}
	return {};
}

namespace {

// Busca en cualquier nivel los elementos fijados de Super Chat (liveChatTickerPaidMessageItemRenderer).
void collectTickerItems(const QJsonValue &value, QJsonArray &out, int depth = 0)
{
	if (depth > 24)
		return;
	if (value.isArray()) {
		for (const QJsonValue v : value.toArray())
			collectTickerItems(v, out, depth + 1);
	} else if (value.isObject()) {
		const QJsonObject o = value.toObject();
		for (auto it = o.begin(); it != o.end(); ++it) {
			if (it.key() == QLatin1String("liveChatTickerPaidMessageItemRenderer"))
				out.append(it.value());
			else
				collectTickerItems(it.value(), out, depth + 1);
		}
	}
}

// Los datos con los que arranca la página del chat (ytInitialData); vacío si no están.
QJsonObject initialData(const QString &html)
{
	static const QString marker = QStringLiteral("window[\"ytInitialData\"] = ");
	const qsizetype start = html.indexOf(marker);
	const qsizetype end = start < 0 ? -1 : html.indexOf(QStringLiteral(";</script>"), start);
	if (end < 0)
		return {};
	return QJsonDocument::fromJson(html.mid(start + marker.size(), end - start - marker.size()).toUtf8())
		.object();
}

// El menú "Chat destacado / Chat en vivo" (sortFilterSubMenuRenderer), en cualquier nivel.
QJsonObject findSortMenu(const QJsonValue &value, int depth = 0)
{
	if (depth > 24)
		return {};
	if (value.isArray()) {
		for (const QJsonValue v : value.toArray()) {
			const QJsonObject found = findSortMenu(v, depth + 1);
			if (!found.isEmpty())
				return found;
		}
	} else if (value.isObject()) {
		const QJsonObject o = value.toObject();
		for (auto it = o.begin(); it != o.end(); ++it) {
			if (it.key() == QLatin1String("sortFilterSubMenuRenderer"))
				return it.value().toObject();
			const QJsonObject found = findSortMenu(it.value(), depth + 1);
			if (!found.isEmpty())
				return found;
		}
	}
	return {};
}

} // namespace

QString firstContinuation(const QString &html)
{
	// YouTube abre el chat en "Chat destacado", que filtra mensajes cuando hay mucho movimiento.
	// La otra opción del menú es "Chat en vivo": todos los mensajes.
	const QJsonArray items = findSortMenu(initialData(html)).value("subMenuItems").toArray();
	for (const QJsonValue v : items) {
		const QJsonObject item = v.toObject();
		const QString c = item.value("continuation")
					  .toObject()
					  .value("reloadContinuationData")
					  .toObject()
					  .value("continuation")
					  .toString();
		if (!item.value("selected").toBool() && !c.isEmpty())
			return c;
	}
	return capture(html, "\"continuation\":\"([^\"]+)\"");
}

int pollDelay(int timeoutMs, int emptyPolls)
{
	// Con el chat en movimiento, cada 2 s; si no llega nada, se va espaciando hasta 5 s.
	const int wanted = 2000 + 1000 * std::clamp(emptyPolls, 0, 3);
	return std::min(wanted, std::clamp(timeoutMs, 1000, 6000));
}

bool SeenIds::add(const QString &id)
{
	if (id.isEmpty())
		return true;
	if (set_.contains(id))
		return false;
	set_.insert(id);
	order_.append(id);
	while (order_.size() > kMax)
		set_.remove(order_.takeFirst());
	return true;
}

QJsonArray pinnedFromPage(const QString &html, const QString &channel)
{
	QJsonArray out;
	const QJsonObject data = initialData(html);
	if (data.isEmpty())
		return out;
	QJsonArray items;
	collectTickerItems(data, items);
	for (const QJsonValue v : items) {
		const QJsonObject item = v.toObject();
		const int remaining = item.value("durationSec").toInt();
		const QJsonObject renderer = item.value("showItemEndpoint")
						     .toObject()
						     .value("showLiveChatItemEndpoint")
						     .toObject()
						     .value("renderer")
						     .toObject();
		if (remaining <= 0 || renderer.isEmpty())
			continue;
		QJsonObject message =
			toMessage(QJsonObject{{"addChatItemAction", QJsonObject{{"item", renderer}}}}, channel);
		QJsonObject paid = message.value("paid").toObject();
		if (paid.isEmpty())
			continue;
		paid.insert("pin", remaining);
		message.insert("paid", paid);
		message.insert("pinned", true);    // ya pasó por el chat: en pantalla solo se fija
		message.insert("recovered", true); // y no se lee en voz alta
		out.append(message);
	}
	return out;
}

QJsonObject toDeletion(const QJsonObject &action)
{
	// A moderator deleted one message (targetItemId) or hid / banned a user (externalChannelId, the
	// same value as authorExternalChannelId on their messages).
	for (const char *key : {"markChatItemAsDeletedAction", "removeChatItemAction"}) {
		const QString id = action.value(QLatin1String(key)).toObject().value("targetItemId").toString();
		if (!id.isEmpty())
			return deleteMessage("youtube", id);
	}
	for (const char *key : {"markChatItemsByAuthorAsDeletedAction", "removeChatItemByAuthorAction"}) {
		const QString channelId =
			action.value(QLatin1String(key)).toObject().value("externalChannelId").toString();
		if (!channelId.isEmpty())
			return deleteAuthor("youtube", channelId, QString());
	}
	return {};
}

QString nextContinuation(const QJsonObject &lc, int &timeoutMs)
{
	timeoutMs = 3000;
	for (const QJsonValue v : lc.value("continuations").toArray()) {
		const QJsonObject c = v.toObject();
		for (const char *k :
		     {"invalidationContinuationData", "timedContinuationData", "reloadContinuationData"}) {
			if (c.contains(k)) {
				const QJsonObject d = c.value(k).toObject();
				timeoutMs = d.value("timeoutMs").toInt(3000);
				return d.value("continuation").toString();
			}
		}
	}
	return {};
}

LiveResult checkLive(const SourceInfo &s)
{
	LiveResult r;
	try {
		if (s.videoFixed) {
			r.live = videoIsLive(s.extra, r.viewers) ? 1 : 0;
			r.videoId = s.extra;
		} else {
			r.videoId = findLiveVideo(s.channel, &r.viewers);
			r.live = r.videoId.isEmpty() ? 0 : 1;
		}
	} catch (const std::exception &) {
		r.live = -1;
	}
	return r;
}

Profile profileFromPage(const QString &html)
{
	// Página de un vídeo: el avatar del canal está en videoOwnerRenderer.
	QString name = capture(html, "\"ownerChannelName\":\"([^\"]+)\"");
	QString avatar = capture(html, "\"videoOwnerRenderer\":\\{\"thumbnail\":\\{\"thumbnails\":\\[\\{\"url\":\"([^\"]+)\"");
	// Página del canal: og:image es la foto de perfil y og:title el nombre.
	if (avatar.isEmpty()) {
		avatar = capture(html, "<meta property=\"og:image\" content=\"([^\"]+)\"");
		name = capture(html, "<meta property=\"og:title\" content=\"([^\"]+)\"");
	}
	if (avatar.startsWith("//"))
		avatar.prepend("https:");
	// Las fotos de YouTube aceptan tamaño y formato en la URL: 88 px y PNG (-rp) en lugar de JPEG.
	static const QRegularExpression size(QStringLiteral("=s\\d+(-[^?]*)?$"));
	if (avatar.contains(size))
		avatar.replace(size, QStringLiteral("=s88-c-k-c0x00ffffff-no-rp"));
	name.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
	name.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
	name.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
	return {name, avatar};
}

Profile fetchProfile(const SourceInfo &s)
{
	const std::string url = s.videoFixed ? ("https://www.youtube.com/watch?v=" + s.extra).toStdString()
					     : channelUrl(s.channel).toStdString();
	net::Response res = net::get(url, kCookies);
	if (!res.ok()) {
		Profile failed;
		failed.failed = isLookupFailure(res.status);
		return failed;
	}
	return profileFromPage(QString::fromUtf8(res.body));
}

class YouTubeConnector : public Connector {
public:
	using Connector::Connector;

protected:
	void session() override
	{
		QString video = source_.extra;
		while (!stopped() && video.isEmpty()) {
			video = findLiveVideo(source_.channel);
			if (!video.isEmpty())
				break;
			report_("waiting", Text{"Detail.WaitingLive", {}});
			if (wait(30000))
				return;
		}
		if (stopped())
			return;

		net::Response page =
			net::get(("https://www.youtube.com/live_chat?is_popout=1&v=" + video).toStdString(), kCookies);
		const QString html = QString::fromUtf8(page.body);
		const QString key = capture(html, "\"INNERTUBE_API_KEY\":\"([^\"]+)\"");
		QString version = capture(html, "\"INNERTUBE_CONTEXT_CLIENT_VERSION\":\"([^\"]+)\"");
		QString continuation = firstContinuation(html);
		if (continuation.isEmpty()) {
			if (!source_.videoFixed)
				source_.extra.clear();
			throw ChatError(Text{"Error.NoLiveChat", {}});
		}
		if (version.isEmpty())
			version = "2.20250101.00.00";
		std::string url = "https://www.youtube.com/youtubei/v1/live_chat/get_live_chat";
		if (!key.isEmpty())
			url += "?key=" + key.toStdString();
		const QJsonObject context{{"client", QJsonObject{{"clientName", "WEB"}, {"clientVersion", version}}}};

		report_("connected", Text());
		// Los Super Chats que YouTube tiene fijados ahora mismo, con el tiempo que les queda (una
		// vez cada uno, aunque se reconecte).
		for (const QJsonValue v : pinnedFromPage(html, source_.channel)) {
			const QJsonObject pinned = v.toObject();
			const QString id = pinned.value("id").toString();
			if (seenPinned_.contains(id))
				continue;
			seenPinned_.insert(id);
			post_(pinned);
		}
		bool first = true;
		int failures = 0, emptyPolls = 0;
		while (!stopped() && !continuation.isEmpty()) {
			QJsonObject body{{"context", context}, {"continuation", continuation}};
			net::Response res = net::postJson(
				url, QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString(), kCookies);
			if (!res.ok()) {
				// Un fallo suelto (red, YouTube ocupado, límite de peticiones) no corta el chat:
				// se reintenta con la misma continuación, que sigue donde se quedó.
				if (++failures > 4)
					throw ChatError(
						Text{"Error.Responded",
						     {QStringLiteral("YouTube"), res.status ? QString::number(res.status)
											     : QString::fromStdString(res.error)}});
				if (wait(res.status == 429 ? 10000 * failures : 2000 * failures))
					return;
				continue;
			}
			failures = 0;
			const QJsonObject lc = QJsonDocument::fromJson(QByteArray::fromStdString(res.body))
						       .object()
						       .value("continuationContents")
						       .toObject()
						       .value("liveChatContinuation")
						       .toObject();
			if (lc.isEmpty())
				break;
			// El primer lote es historial: al conectar por primera vez no se repite; al reconectar
			// se muestran solo los mensajes que se escribieron mientras no había conexión.
			const bool history = first && !connectedBefore_;
			int posted = 0;
			for (const QJsonValue a : lc.value("actions").toArray()) {
				QJsonObject m = toMessage(a.toObject(), source_.channel);
				if (m.isEmpty()) {
					m = toDeletion(a.toObject());
					if (first)
						continue;
				} else if (!seen_.add(m.value("id").toString()) || history) {
					continue;
				}
				if (!m.isEmpty()) {
					post_(m);
					++posted;
				}
			}
			first = false;
			connectedBefore_ = true;
			emptyPolls = posted ? 0 : emptyPolls + 1;
			int timeoutMs = 3000;
			continuation = nextContinuation(lc, timeoutMs);
			if (wait(pollDelay(timeoutMs, emptyPolls)))
				return;
		}
		if (!source_.videoFixed)
			source_.extra.clear(); // el directo terminó: la próxima vez se busca uno nuevo
		throw ChatError(Text{"Error.LiveChatEnded", {}});
	}

private:
	QSet<QString> seenPinned_;
	SeenIds seen_;
	bool connectedBefore_ = false;
};

std::shared_ptr<Connector> create(const SourceInfo &s, Connector::PostFn post, Connector::ReportFn report)
{
	return std::make_shared<YouTubeConnector>(s, std::move(post), std::move(report));
}

} // namespace youtube
} // namespace chat
