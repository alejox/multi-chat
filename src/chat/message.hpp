#pragma once

// Modelo común de un mensaje de chat. Es un QJsonObject para mandarlo tal cual al overlay:
//   { id, platform, channel, source, author, authorId, color, badges[], parts[], kind, amount, system,
//     systemKey, systemArgs[], authorKey, action, recovered, ts }
//   system/author hold English until ChatManager calls localizeMessage().
//   parts: [{type:"text", text}, {type:"emote", url, name}]
//   kind:  "chat" | "highlight" (Super Chat, bits, subs, raids...)

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace chat {

// Messages kept in memory: the chat manager's history (what undo and "feature" can reach) and the
// dock's view hold the same amount.
constexpr int kHistoryLimit = 300;

// A user-facing text built off the main thread (connectors) and translated on it: a locale key
// from data/locale plus its %1..%N arguments. An empty key means args[0] is shown as is.
struct Text {
	QString key;
	QStringList args;

	static Text raw(const QString &text) { return Text{QString(), {text}}; }
	bool isEmpty() const { return key.isEmpty() && args.value(0).isEmpty(); }
};

// Looks up a locale key; returns an empty string (or the key itself, like obs_module_text) when
// the key is missing.
using Translator = std::function<QString(const QString &key)>;

// Replaces %1..%99 in one pass: a "%2" inside an argument stays as text.
QString fillArgs(const QString &pattern, const QStringList &args);
// Built-in English patterns of every Text key (the same values as en-US.ini).
const QHash<QString, QString> &englishTexts();
// The text in the translator's language, falling back to English, then to the key.
QString translate(const Text &text, const Translator &translator = {});
// Replaces `system` and `author` with their translation when the message carries `systemKey` /
// `authorKey` (set by connectors through MessageOptions). Called on the main thread.
void localizeMessage(QJsonObject &message, const Translator &translator);

struct MessageOptions {
	QString color;
	QStringList badges; // owner, moderator, vip, subscriber, member, verified, staff
	QString kind = QStringLiteral("chat");
	QString amount;
	QString system;    // platform-provided system line, shown as is
	Text systemText;   // or a translatable one (wins over `system`): {"Event.Sub", {"Ana"}}
	QString authorKey; // translatable author name ("Author.Anonymous"), replaces `author`
	QString id;
	QString authorId; // stable platform user id, used to match moderation events
	bool action = false; // /me de Twitch
	bool recovered = false; // llegó tarde al reconectar: se muestra, pero no se lee en voz alta
};

QJsonObject textPart(const QString &text);
QJsonObject emotePart(const QString &url, const QString &name);
QJsonObject makeMessage(const QString &platform, const QString &channel, const QString &author, const QJsonArray &parts,
			const MessageOptions &options = {});

// Moderation event: a message deleted by the platform, a user banned or timed out, or the whole
// chat cleared. Posted by connectors through the same PostFn as messages:
//   { type: "delete", platform, source, id?, authorId?, author?, all? }
// ChatManager adds `ids` (the history messages it removed) before emitting it.
QJsonObject deleteMessage(const QString &platform, const QString &id);
QJsonObject deleteAuthor(const QString &platform, const QString &authorId, const QString &author);
QJsonObject clearChannel(const QString &platform);
bool isDeletion(const QJsonObject &object);
// True if the deletion event affects the message. Never matches across sources (channels).
bool deletionMatches(const QJsonObject &deletion, const QJsonObject &message);
// Local moderation from the dock ("Hide message", "Hide user"): deletion events built from a
// stored message, scoped to its source. Empty if the message lacks the needed fields.
QJsonObject hideMessage(const QJsonObject &message);
QJsonObject hideAuthor(const QJsonObject &message);

// The message text as plain text: text parts as they are, emotes by their name.
QString messageText(const QJsonObject &message);
// "Author: text" (or "Author text" for /me), for the clipboard.
QString plainText(const QJsonObject &message);
// Dock search: every word of the query appears in the author, the text (emote names included)
// or the system line, ignoring case and accents. An empty query matches everything.
bool matchesSearch(const QJsonObject &message, const QString &query);

// Streamer-side filters, applied by ChatManager before a message is stored or shown.
struct FilterRules {
	QStringList hiddenUsers;  // exact names, case- and accent-insensitive
	bool hideCommands = true; // chat messages starting with "!"
	QStringList blockedWords; // whole words or phrases, case- and accent-insensitive

	// Known bots hidden, commands hidden, no blocked words.
	static FilterRules defaults();
	// Missing keys keep their default value.
	static FilterRules fromJson(const QJsonObject &json);
	QJsonObject toJson() const;
};
QStringList defaultHiddenUsers();
// Lower case without accents or other combining marks (NFD), for comparisons.
QString foldText(const QString &text);
// True if the message must not be shown. Highlights (subs, Super Chats, donations...) are never
// hidden by the command rule.
bool isFiltered(const QJsonObject &message, const FilterRules &rules);

// Auto-hide rule of one "Multichat (featured)" source: the message stays baseSeconds plus
// perCharMs per character, at most maxSeconds. Disabled = it stays until removed by hand.
struct FeaturedHidePolicy {
	bool enabled = true;
	int baseSeconds = 8;
	int perCharMs = 60;
	int maxSeconds = 30;
};
// Characters that count for the time on screen: the system line and the text parts; an
// emote counts as two.
int featuredTextLength(const QJsonObject &message);
// Milliseconds the message stays, or -1 when the policy never hides it.
int featuredDurationMs(int textLength, const FeaturedHidePolicy &policy);
// With several featured sources the shortest enabled policy decides; -1 if none hides.
int featuredDurationMs(int textLength, const QList<FeaturedHidePolicy> &policies);

// Nombre visible de la plataforma.
QString platformName(const QString &platform);

// Texto escapado para HTML con los enlaces (http://, https://, www.) como <a href>.
QString linkifyHtml(const QString &text, const QString &linkColor = {});

} // namespace chat
