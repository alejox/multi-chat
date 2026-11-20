#pragma once

// YouTube's official API for live chat (liveChatMessages.streamList): the server pushes each
// message through a connection it keeps open, instead of the plugin asking every few seconds.
// It needs the user to sign in with Google (OAuth 2.0 for desktop apps, with PKCE).

#include "message.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace chat {
namespace youtube {
namespace api {

// What identifies the app before Google (client id and secret of an OAuth "Desktop app" client;
// for this kind of client the secret is not confidential) and the user's sign-in (refresh token).
struct Credentials {
	QString clientId;
	QString clientSecret;
	QString refreshToken;

	bool operator==(const Credentials &o) const
	{
		return clientId == o.clientId && clientSecret == o.clientSecret && refreshToken == o.refreshToken;
	}
};

// Thread-safe. Set from the main thread (config and sign-in); read by the YouTube connectors.
void setCredentials(const Credentials &credentials);
Credentials credentials();
// There is a client and a sign-in to use.
bool signedIn();
// Changes every time setCredentials changes something: connectors restart their session.
quint64 generation();

// What the connectors last found, for the settings page.
struct Status {
	bool active = false; // a chat is being read through the API right now
	QString problem;     // why the API could not be used (English, technical); empty if none
};
Status status();
void setActive(bool active);
void setProblem(const QString &problem);

// --- Sign-in (pure helpers) ---------------------------------------------------------------
// A random PKCE verifier (64 URL-safe characters) and its S256 challenge.
QString makeVerifier();
QString challengeFor(const QString &verifier);
// The Google page the browser opens to ask for permission to read YouTube (read-only).
QString authUrl(const QString &clientId, const QString &redirectUri, const QString &challenge, const QString &state);
// Body (form-encoded) of the two calls to the token endpoint.
QByteArray codeExchangeBody(const Credentials &credentials, const QString &code, const QString &verifier,
			    const QString &redirectUri);
QByteArray refreshBody(const Credentials &credentials);
struct Tokens {
	QString accessToken;
	QString refreshToken; // only when exchanging a code
	int expiresIn = 0;    // seconds
	QString error;        // Google's "error" (plus description), empty if it went well
};
Tokens parseTokens(const QByteArray &json);
// "code" and "state" from the first line of the browser's request to the local page
// ("GET /?state=..&code=.. HTTP/1.1"); `error` gets Google's error ("access_denied") if any.
bool parseRedirect(const QByteArray &requestLine, QString &code, QString &state, QString &error);

// --- Sign-in (network, blocking) ----------------------------------------------------------
Tokens exchangeCode(const Credentials &credentials, const QString &code, const QString &verifier,
		    const QString &redirectUri);
// A valid access token, asking Google for a new one when the last expired. Empty (and `error`)
// if there is no sign-in or Google refused.
QString accessToken(QString *error = nullptr);
// Forget the cached access token (the API answered 401).
void dropAccessToken();

// --- Chat ---------------------------------------------------------------------------------
// The live chat id of a video (videos.list, 1 quota unit). Empty and `error` if it has none.
QString liveChatId(const QString &videoId, const QString &token, QString *error = nullptr);
QString streamUrl(const QString &liveChatId, const QString &pageToken);

// Splits what the server sends (a JSON array that never ends: "[{..}\n,\n{..}") into its objects.
class ObjectSplitter {
public:
	void feed(const QByteArray &data) { pending_.append(data); }
	// The next complete top-level object; false if more data is needed.
	bool next(QByteArray &object);

private:
	QByteArray pending_;
	qsizetype scanned_ = 0; // bytes of pending_ already looked at
	qsizetype start_ = -1;  // where the object being read begins
	int depth_ = 0;
	bool inString_ = false;
	bool escaped_ = false;
};

// "reason" of an error object ({"error":{"errors":[{"reason":"liveChatEnded"}],"code":403}}), or
// its status/code as text; empty if the object is not an error.
QString errorReason(const QJsonObject &response);
// A liveChatMessage resource as a chat message (same shape as youtube::toMessage); empty for
// the kinds that are not shown (deleted, banned, chat ended, polls...).
QJsonObject toMessage(const QJsonObject &item, const QString &channel);
// messageDeletedEvent / userBannedEvent as a moderation event; empty otherwise.
QJsonObject toDeletion(const QJsonObject &item);
// snippet.publishedAt in ms since the epoch; 0 if missing.
qint64 publishedMs(const QJsonObject &item);
// snippet.type == chatEndedEvent.
bool isChatEnded(const QJsonObject &item);

} // namespace api
} // namespace youtube
} // namespace chat
