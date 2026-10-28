#include "message.hpp"

#include <QDateTime>
#include <QRegularExpression>
#include <QUrl>
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

QString linkifyHtml(const QString &text, const QString &linkColor)
{
	static const QRegularExpression url(QStringLiteral("\\b(?:https?://|www\\.)[^\\s<>\"]+"),
					    QRegularExpression::CaseInsensitiveOption);
	// Puntuación que suele ir pegada al final de un enlace pero no forma parte de él.
	static const QString trailing = QStringLiteral(".,;:!?)]}'\"");
	const QString style = linkColor.isEmpty() ? QString() : QStringLiteral(" style=\"color:%1;\"").arg(linkColor);
	QString out;
	qsizetype cur = 0;
	auto it = url.globalMatch(text);
	while (it.hasNext()) {
		const auto m = it.next();
		QString link = m.captured();
		while (!link.isEmpty() && trailing.contains(link.back()) &&
		       !(link.back() == ')' && link.count('(') >= link.count(')')))
			link.chop(1);
		if (link.size() <= 4)
			continue;
		const QString href = link.startsWith(QStringLiteral("www."), Qt::CaseInsensitive) ? "https://" + link : link;
		out += text.mid(cur, m.capturedStart() - cur).toHtmlEscaped();
		out += QStringLiteral("<a href=\"%1\"%2>%3</a>")
			       .arg(QString::fromUtf8(QUrl::fromUserInput(href).toEncoded()).toHtmlEscaped(), style,
				    link.toHtmlEscaped());
		cur = m.capturedStart() + link.size();
	}
	out += text.mid(cur).toHtmlEscaped();
	return out;
}

} // namespace chat
