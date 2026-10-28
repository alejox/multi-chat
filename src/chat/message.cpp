#include "message.hpp"

#include <QDateTime>
#include <QUuid>

namespace chat {

QJsonObject textPart(const QString &text)
{
	return QJsonObject{{"type", "text"}, {"text", text}};
}

QJsonObject emotePart(const QString &url, const QString &name)
{
	return QJsonObject{{"type", "emote"}, {"url", url}, {"name", name}};
}

QJsonObject makeMessage(const QString &platform, const QString &channel, const QString &author, const QJsonArray &parts,
			const MessageOptions &o)
{
	QJsonObject m{
		{"id", o.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : o.id},
		{"platform", platform},
		{"channel", channel},
		{"author", author},
		{"badges", QJsonArray::fromStringList(o.badges)},
		{"parts", parts},
		{"kind", o.kind},
		{"ts", QDateTime::currentMSecsSinceEpoch() / 1000.0},
	};
	if (!o.color.isEmpty())
		m.insert("color", o.color);
	if (!o.amount.isEmpty())
		m.insert("amount", o.amount);
	if (!o.system.isEmpty())
		m.insert("system", o.system);
	if (o.action)
		m.insert("action", true);
	return m;
}

QString platformName(const QString &platform)
{
	if (platform == "twitch")
		return QStringLiteral("Twitch");
	if (platform == "youtube")
		return QStringLiteral("YouTube");
	if (platform == "kick")
		return QStringLiteral("Kick");
	if (platform == "tiktok")
		return QStringLiteral("TikTok");
	if (platform == "demo")
		return QStringLiteral("Demo");
	return platform;
}

} // namespace chat
