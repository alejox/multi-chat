#include "youtube-api.hpp"

#include "../net/net.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <chrono>
#include <mutex>

namespace chat {
namespace youtube {
namespace api {

namespace {

const char *const kTokenUrl = "https://oauth2.googleapis.com/token";
const char *const kApiUrl = "https://youtube.googleapis.com/youtube/v3";
const char *const kScope = "https://www.googleapis.com/auth/youtube.readonly";

std::mutex mutex;
Credentials current;
quint64 currentGeneration = 1;
QString cachedToken;
std::chrono::steady_clock::time_point cachedUntil;
Status currentStatus;

QByteArray form(const QList<QPair<QString, QString>> &fields)
{
	QUrlQuery query;
	for (const auto &[name, value] : fields)
		query.addQueryItem(name, QString::fromUtf8(QUrl::toPercentEncoding(value)));
	return query.toString(QUrl::FullyEncoded).toUtf8();
}

Tokens postTokens(const QByteArray &body)
{
	const net::Response res = net::request("POST", kTokenUrl,
					       {{"Content-Type", "application/x-www-form-urlencoded"}},
					       body.toStdString(), 15000);
	if (res.status == 0) {
		Tokens failed;
		failed.error = QString::fromStdString(res.error);
		return failed;
	}
	Tokens tokens = parseTokens(QByteArray::fromStdString(res.body));
	if (tokens.accessToken.isEmpty() && tokens.error.isEmpty())
		tokens.error = QStringLiteral("HTTP %1").arg(res.status);
	return tokens;
}

// Colores de cada nivel de Super Chat, como los pinta YouTube: {cabecera, cuerpo, texto oscuro}.
struct Tier {
	const char *header;
	const char *body;
	bool darkText;
	int pin; // segundos que YouTube lo deja fijado
};
const Tier kTiers[] = {
	{"#1565c0", "#1e88e5", false, 0},   {"#00b8d4", "#00e5ff", true, 0},     {"#00bfa5", "#1de9b6", true, 120},
	{"#ffb300", "#ffca28", true, 300},  {"#e65100", "#f57c00", false, 600},  {"#c2185b", "#e91e63", false, 1800},
	{"#d00000", "#e62117", false, 3600},
};

QJsonObject paidForTier(int tier)
{
	if (tier < 1)
		return {};
	const Tier &t = kTiers[std::min(tier, 7) - 1];
	const QString text = t.darkText ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
	return {{"header", t.header}, {"body", t.body}, {"headerText", text}, {"bodyText", text}, {"pin", t.pin}};
}

QJsonArray textParts(const QString &text)
{
	QJsonArray parts;
	if (!text.isEmpty())
		parts.append(textPart(text));
	return parts;
}

} // namespace

void setCredentials(const Credentials &credentials)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (credentials == current)
		return;
	current = credentials;
	cachedToken.clear();
	currentStatus = {};
	++currentGeneration;
}

Credentials credentials()
{
	std::lock_guard<std::mutex> lock(mutex);
	return current;
}

bool signedIn()
{
	std::lock_guard<std::mutex> lock(mutex);
	return !current.clientId.isEmpty() && !current.refreshToken.isEmpty();
}

quint64 generation()
{
	std::lock_guard<std::mutex> lock(mutex);
	return currentGeneration;
}

Status status()
{
	std::lock_guard<std::mutex> lock(mutex);
	return currentStatus;
}

void setActive(bool active)
{
	std::lock_guard<std::mutex> lock(mutex);
	currentStatus.active = active;
	if (active)
		currentStatus.problem.clear();
}

void setProblem(const QString &problem)
{
	std::lock_guard<std::mutex> lock(mutex);
	currentStatus.problem = problem;
}

QString makeVerifier()
{
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
	QString out;
	for (int i = 0; i < 64; ++i)
		out += QLatin1Char(alphabet[QRandomGenerator::system()->bounded(int(sizeof(alphabet) - 1))]);
	return out;
}

QString challengeFor(const QString &verifier)
{
	return QString::fromLatin1(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
					   .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString authUrl(const QString &clientId, const QString &redirectUri, const QString &challenge, const QString &state)
{
	return QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth?") +
	       QString::fromUtf8(form({{"client_id", clientId},
				       {"redirect_uri", redirectUri},
				       {"response_type", "code"},
				       {"scope", kScope},
				       {"code_challenge", challenge},
				       {"code_challenge_method", "S256"},
				       {"state", state},
				       // Sin esto Google no da el token que renueva la sesión.
				       {"access_type", "offline"},
				       {"prompt", "consent"}}));
}

QByteArray codeExchangeBody(const Credentials &c, const QString &code, const QString &verifier,
			    const QString &redirectUri)
{
	return form({{"client_id", c.clientId},
		     {"client_secret", c.clientSecret},
		     {"code", code},
		     {"code_verifier", verifier},
		     {"grant_type", "authorization_code"},
		     {"redirect_uri", redirectUri}});
}

QByteArray refreshBody(const Credentials &c)
{
	return form({{"client_id", c.clientId},
		     {"client_secret", c.clientSecret},
		     {"refresh_token", c.refreshToken},
		     {"grant_type", "refresh_token"}});
}

Tokens parseTokens(const QByteArray &json)
{
	Tokens tokens;
	const QJsonObject o = QJsonDocument::fromJson(json).object();
	tokens.accessToken = o.value("access_token").toString();
	tokens.refreshToken = o.value("refresh_token").toString();
	tokens.expiresIn = o.value("expires_in").toInt();
	tokens.error = o.value("error").toString();
	const QString description = o.value("error_description").toString();
	if (!tokens.error.isEmpty() && !description.isEmpty())
		tokens.error += QStringLiteral(": ") + description;
	return tokens;
}

bool parseRedirect(const QByteArray &requestLine, QString &code, QString &state, QString &error)
{
	const QList<QByteArray> words = requestLine.trimmed().split(' ');
	if (words.size() < 2 || words[0] != "GET")
		return false;
	const QUrlQuery query(QUrl::fromEncoded(words[1]).query(QUrl::FullyEncoded));
	code = query.queryItemValue("code", QUrl::FullyDecoded);
	state = query.queryItemValue("state", QUrl::FullyDecoded);
	error = query.queryItemValue("error", QUrl::FullyDecoded);
	return !code.isEmpty() || !error.isEmpty();
}

Tokens exchangeCode(const Credentials &c, const QString &code, const QString &verifier, const QString &redirectUri)
{
	return postTokens(codeExchangeBody(c, code, verifier, redirectUri));
}

QString accessToken(QString *error)
{
	Credentials c;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!cachedToken.isEmpty() && std::chrono::steady_clock::now() < cachedUntil)
			return cachedToken;
		c = current;
	}
	if (c.clientId.isEmpty() || c.refreshToken.isEmpty()) {
		if (error)
			*error = QStringLiteral("not signed in");
		return {};
	}
	const Tokens tokens = postTokens(refreshBody(c));
	if (tokens.accessToken.isEmpty()) {
		if (error)
			*error = QStringLiteral("sign-in refused: ") + tokens.error;
		return {};
	}
	std::lock_guard<std::mutex> lock(mutex);
	if (c == current) {
		cachedToken = tokens.accessToken;
		// Un minuto de margen antes de que caduque.
		cachedUntil = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(60, tokens.expiresIn) - 60);
	}
	return tokens.accessToken;
}

void dropAccessToken()
{
	std::lock_guard<std::mutex> lock(mutex);
	cachedToken.clear();
}

QString liveChatId(const QString &videoId, const QString &token, QString *error)
{
	const QString url = QStringLiteral("%1/videos?part=liveStreamingDetails&id=%2")
				    .arg(QLatin1String(kApiUrl), QString::fromUtf8(QUrl::toPercentEncoding(videoId)));
	const net::Response res =
		net::get(url.toStdString(), {{"Authorization", "Bearer " + token.toStdString()}}, 15000);
	const QJsonObject o = QJsonDocument::fromJson(QByteArray::fromStdString(res.body)).object();
	if (!res.ok()) {
		if (error) {
			const QString reason = errorReason(o);
			*error = QStringLiteral("videos.list: ") +
				 (reason.isEmpty() ? (res.status ? QStringLiteral("HTTP %1").arg(res.status)
								 : QString::fromStdString(res.error))
						   : reason);
		}
		return {};
	}
	const QString id = o.value("items")
				   .toArray()
				   .at(0)
				   .toObject()
				   .value("liveStreamingDetails")
				   .toObject()
				   .value("activeLiveChatId")
				   .toString();
	if (id.isEmpty() && error)
		*error = QStringLiteral("videos.list: the video has no active live chat");
	return id;
}

QString streamUrl(const QString &liveChatId, const QString &pageToken)
{
	QString url = QStringLiteral("%1/liveChat/messages/stream?part=id,snippet,authorDetails&liveChatId=%2")
			      .arg(QLatin1String(kApiUrl), QString::fromUtf8(QUrl::toPercentEncoding(liveChatId)));
	if (!pageToken.isEmpty())
		url += QStringLiteral("&pageToken=") + QString::fromUtf8(QUrl::toPercentEncoding(pageToken));
	return url;
}

bool ObjectSplitter::next(QByteArray &object)
{
	for (; scanned_ < pending_.size(); ++scanned_) {
		const char c = pending_[scanned_];
		if (start_ < 0) {
			// Entre objetos solo hay "[", ",", "]" y espacios.
			if (c == '{') {
				start_ = scanned_;
				depth_ = 1;
			}
			continue;
		}
		if (inString_) {
			if (escaped_)
				escaped_ = false;
			else if (c == '\\')
				escaped_ = true;
			else if (c == '"')
				inString_ = false;
		} else if (c == '"') {
			inString_ = true;
		} else if (c == '{') {
			++depth_;
		} else if (c == '}' && --depth_ == 0) {
			object = pending_.mid(start_, scanned_ - start_ + 1);
			pending_.remove(0, scanned_ + 1);
			scanned_ = 0;
			start_ = -1;
			return true;
		}
	}
	if (start_ < 0) {
		pending_.clear();
		scanned_ = 0;
	}
	return false;
}

QString errorReason(const QJsonObject &response)
{
	if (!response.contains("error"))
		return {};
	const QJsonObject error = response.value("error").toObject();
	const QString reason = error.value("errors").toArray().at(0).toObject().value("reason").toString();
	if (!reason.isEmpty())
		return reason;
	const QString status = error.value("status").toString();
	return status.isEmpty() ? QStringLiteral("HTTP %1").arg(error.value("code").toInt()) : status;
}

qint64 publishedMs(const QJsonObject &item)
{
	const QString at = item.value("snippet").toObject().value("publishedAt").toString();
	const QDateTime time = QDateTime::fromString(at, Qt::ISODateWithMs);
	return time.isValid() ? time.toMSecsSinceEpoch() : 0;
}

bool isChatEnded(const QJsonObject &item)
{
	return item.value("snippet").toObject().value("type").toString() == QLatin1String("chatEndedEvent");
}

QJsonObject toMessage(const QJsonObject &item, const QString &channel)
{
	const QJsonObject snippet = item.value("snippet").toObject();
	const QJsonObject author = item.value("authorDetails").toObject();
	const QString type = snippet.value("type").toString();
	const QString display = snippet.value("displayMessage").toString();

	MessageOptions o;
	o.id = item.value("id").toString();
	o.authorId = author.value("channelId").toString();
	if (o.authorId.isEmpty())
		o.authorId = snippet.value("authorChannelId").toString();
	if (author.value("isChatOwner").toBool())
		o.badges << QStringLiteral("owner");
	if (author.value("isChatModerator").toBool())
		o.badges << QStringLiteral("moderator");
	if (author.value("isVerified").toBool())
		o.badges << QStringLiteral("verified");
	if (author.value("isChatSponsor").toBool())
		o.badges << QStringLiteral("member");

	QString text;
	QJsonObject paid;
	if (type == QLatin1String("textMessageEvent")) {
		text = snippet.value("textMessageDetails").toObject().value("messageText").toString();
		if (text.isEmpty())
			text = display;
	} else if (type == QLatin1String("superChatEvent")) {
		const QJsonObject d = snippet.value("superChatDetails").toObject();
		o.kind = QStringLiteral("highlight");
		o.amount = d.value("amountDisplayString").toString();
		text = d.value("userComment").toString();
		paid = paidForTier(d.value("tier").toInt());
	} else if (type == QLatin1String("superStickerEvent")) {
		const QJsonObject d = snippet.value("superStickerDetails").toObject();
		o.kind = QStringLiteral("highlight");
		o.amount = d.value("amountDisplayString").toString();
		// La API no da la imagen del sticker, solo su descripción.
		text = d.value("superStickerMetadata").toObject().value("altText").toString();
		paid = paidForTier(d.value("tier").toInt());
	} else if (type == QLatin1String("memberMilestoneChatEvent")) {
		o.kind = QStringLiteral("highlight");
		text = snippet.value("memberMilestoneChatDetails").toObject().value("userComment").toString();
		o.system = display;
	} else if (type == QLatin1String("newSponsorEvent") || type == QLatin1String("membershipGiftingEvent") ||
		   type == QLatin1String("giftMembershipReceivedEvent")) {
		o.kind = QStringLiteral("highlight");
		o.system = display;
	} else {
		return {};
	}
	QString name = author.value("displayName").toString();
	if (name.isEmpty())
		name = QStringLiteral("?");
	QJsonObject message = makeMessage(QStringLiteral("youtube"), channel, name, textParts(text), o);
	if (!paid.isEmpty())
		message.insert("paid", paid);
	return message;
}

QJsonObject toDeletion(const QJsonObject &item)
{
	const QJsonObject snippet = item.value("snippet").toObject();
	const QString type = snippet.value("type").toString();
	if (type == QLatin1String("messageDeletedEvent")) {
		const QString id = snippet.value("messageDeletedDetails").toObject().value("deletedMessageId").toString();
		return id.isEmpty() ? QJsonObject() : deleteMessage(QStringLiteral("youtube"), id);
	}
	if (type == QLatin1String("userBannedEvent")) {
		const QJsonObject banned =
			snippet.value("userBannedDetails").toObject().value("bannedUserDetails").toObject();
		const QString id = banned.value("channelId").toString();
		return id.isEmpty() ? QJsonObject()
				    : deleteAuthor(QStringLiteral("youtube"), id, banned.value("displayName").toString());
	}
	return {};
}

} // namespace api
} // namespace youtube
} // namespace chat
