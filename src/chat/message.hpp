#pragma once

// Modelo común de un mensaje de chat. Es un QJsonObject para mandarlo tal cual al overlay:
//   { id, platform, channel, source, author, authorId, color, badges[], parts[], kind, amount, system,
//     action, recovered, ts }
//   parts: [{type:"text", text}, {type:"emote", url, name}]
//   kind:  "chat" | "highlight" (Super Chat, bits, subs, raids...)

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace chat {

struct MessageOptions {
	QString color;
	QStringList badges; // owner, moderator, vip, subscriber, member, verified, staff
	QString kind = QStringLiteral("chat");
	QString amount;
	QString system; // texto de sistema: "se suscribió por 6 meses"
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

// Nombre visible de la plataforma.
QString platformName(const QString &platform);

// Texto escapado para HTML con los enlaces (http://, https://, www.) como <a href>.
QString linkifyHtml(const QString &text, const QString &linkColor = {});

} // namespace chat
