// YouTube: lee el chat público del directo con el endpoint interno de la web (sin API key).

#include "platforms.hpp"
#include "message.hpp"

#include <QJsonDocument>
#include <QRegularExpression>

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
	for (const QJsonValue &r : obj.value("runs").toArray())
		out += r.toObject().value("text").toString();
	return out;
}

QJsonArray runsToParts(const QJsonArray &runs)
{
	QJsonArray parts;
	for (const QJsonValue &v : runs) {
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
	for (const QJsonValue &v : renderer.value("authorBadges").toArray()) {
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

QString findLiveVideo(const QString &channel)
{
	net::Response res = net::get((channelUrl(channel) + "/live").toStdString(), kCookies);
	if (!res.ok())
		throw ChatError(
			QStringLiteral("YouTube respondió %1")
				.arg(res.status ? QString::number(res.status) : QString::fromStdString(res.error)));
	return liveVideoFromPage(QString::fromUtf8(res.body));
}

bool videoIsLive(const QString &videoId)
{
	net::Response res = net::get(("https://www.youtube.com/watch?v=" + videoId).toStdString(), kCookies);
	if (!res.ok())
		throw ChatError("no se pudo abrir el vídeo de YouTube");
	return QString::fromUtf8(res.body).contains("\"isLiveNow\":true");
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
		if (QString::fromLatin1(key) == "liveChatMembershipItemRenderer")
			o.system = simpleText(r.value("headerSubtext").toObject());
		QString author = simpleText(r.value("authorName").toObject());
		return makeMessage("youtube", channel, author.isEmpty() ? QStringLiteral("?") : author,
				   runsToParts(r.value("message").toObject().value("runs").toArray()), o);
	}
	return {};
}

QString nextContinuation(const QJsonObject &lc, int &timeoutMs)
{
	timeoutMs = 3000;
	for (const QJsonValue &v : lc.value("continuations").toArray()) {
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
			r.live = videoIsLive(s.extra) ? 1 : 0;
			r.videoId = s.extra;
		} else {
			r.videoId = findLiveVideo(s.channel);
			r.live = r.videoId.isEmpty() ? 0 : 1;
		}
	} catch (const std::exception &) {
		r.live = -1;
	}
	return r;
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
			report_("waiting", "esperando a que empiece el directo");
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
		QString continuation = capture(html, "\"continuation\":\"([^\"]+)\"");
		if (continuation.isEmpty()) {
			if (!source_.videoFixed)
				source_.extra.clear();
			throw ChatError("el directo no tiene chat disponible");
		}
		if (version.isEmpty())
			version = "2.20250101.00.00";
		std::string url = "https://www.youtube.com/youtubei/v1/live_chat/get_live_chat";
		if (!key.isEmpty())
			url += "?key=" + key.toStdString();
		const QJsonObject context{{"client", QJsonObject{{"clientName", "WEB"}, {"clientVersion", version}}}};

		report_("connected", QString());
		bool first = true;
		while (!stopped() && !continuation.isEmpty()) {
			QJsonObject body{{"context", context}, {"continuation", continuation}};
			net::Response res = net::postJson(
				url, QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString(), kCookies);
			if (!res.ok())
				throw ChatError(QStringLiteral("YouTube respondió %1")
							.arg(res.status ? QString::number(res.status)
									: QString::fromStdString(res.error)));
			const QJsonObject lc = QJsonDocument::fromJson(QByteArray::fromStdString(res.body))
						       .object()
						       .value("continuationContents")
						       .toObject()
						       .value("liveChatContinuation")
						       .toObject();
			if (lc.isEmpty())
				break;
			// El primer lote es historial antiguo: no lo repetimos.
			if (!first) {
				for (const QJsonValue &a : lc.value("actions").toArray()) {
					QJsonObject m = toMessage(a.toObject(), source_.channel);
					if (!m.isEmpty())
						post_(m);
				}
			}
			first = false;
			int timeoutMs = 3000;
			continuation = nextContinuation(lc, timeoutMs);
			if (wait(std::clamp(timeoutMs, 1000, 6000)))
				return;
		}
		if (!source_.videoFixed)
			source_.extra.clear(); // el directo terminó: la próxima vez se busca uno nuevo
		throw ChatError("el chat del directo terminó");
	}
};

std::shared_ptr<Connector> create(const SourceInfo &s, Connector::PostFn post, Connector::ReportFn report)
{
	return std::make_shared<YouTubeConnector>(s, std::move(post), std::move(report));
}

} // namespace youtube
} // namespace chat
