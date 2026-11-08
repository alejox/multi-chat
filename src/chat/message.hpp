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

// Nombre visible de la plataforma.
QString platformName(const QString &platform);

// Texto escapado para HTML con los enlaces (http://, https://, www.) como <a href>.
QString linkifyHtml(const QString &text, const QString &linkColor = {});

} // namespace chat
