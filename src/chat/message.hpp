#pragma once

// Modelo común de un mensaje de chat. Es un QJsonObject para mandarlo tal cual al overlay:
//   { id, platform, channel, source, author, color, badges[], parts[], kind, amount, system, action, ts }
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
	bool action = false; // /me de Twitch
};

QJsonObject textPart(const QString &text);
QJsonObject emotePart(const QString &url, const QString &name);
QJsonObject makeMessage(const QString &platform, const QString &channel, const QString &author, const QJsonArray &parts,
			const MessageOptions &options = {});

// Nombre visible de la plataforma.
QString platformName(const QString &platform);

// Texto escapado para HTML con los enlaces (http://, https://, www.) como <a href>.
QString linkifyHtml(const QString &text, const QString &linkColor = {});

} // namespace chat
