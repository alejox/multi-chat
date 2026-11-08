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
	if (!o.authorId.isEmpty())
		m.insert("authorId", o.authorId);
	if (o.action)
		m.insert("action", true);
	if (o.recovered)
		m.insert("recovered", true);
	return m;
}

QJsonObject deleteMessage(const QString &platform, const QString &id)
{
	return QJsonObject{{"type", "delete"}, {"platform", platform}, {"id", id}};
}

QJsonObject deleteAuthor(const QString &platform, const QString &authorId, const QString &author)
{
	QJsonObject e{{"type", "delete"}, {"platform", platform}};
	if (!authorId.isEmpty())
		e.insert("authorId", authorId);
	if (!author.isEmpty())
		e.insert("author", author);
	return e;
}

QJsonObject clearChannel(const QString &platform)
{
	return QJsonObject{{"type", "delete"}, {"platform", platform}, {"all", true}};
}

bool isDeletion(const QJsonObject &object)
{
	return object.value("type").toString() == QLatin1String("delete");
}

bool deletionMatches(const QJsonObject &d, const QJsonObject &m)
{
	if (!isDeletion(d) || d.value("source").toString() != m.value("source").toString() ||
	    d.value("platform").toString() != m.value("platform").toString())
		return false;
	if (d.value("all").toBool())
		return true;
	const QString id = d.value("id").toString();
	if (!id.isEmpty())
		return id == m.value("id").toString();
	const QString authorId = d.value("authorId").toString();
	const QString messageAuthorId = m.value("authorId").toString();
	if (!authorId.isEmpty() && !messageAuthorId.isEmpty())
		return authorId == messageAuthorId;
	const QString author = d.value("author").toString();
	return !author.isEmpty() && author.compare(m.value("author").toString(), Qt::CaseInsensitive) == 0;
}

QStringList defaultHiddenUsers()
{
	return {"Nightbot", "StreamElements", "Streamlabs", "Moobot", "Fossabot", "Wizebot", "Botrix"};
}

FilterRules FilterRules::defaults()
{
	FilterRules r;
	r.hiddenUsers = defaultHiddenUsers();
	return r;
}

FilterRules FilterRules::fromJson(const QJsonObject &json)
{
	FilterRules r = defaults();
	const auto list = [](const QJsonValue &v) {
		QStringList out;
		for (const QJsonValue item : v.toArray())
			out << item.toString();
		return out;
	};
	if (json.contains("hiddenUsers"))
		r.hiddenUsers = list(json.value("hiddenUsers"));
	if (json.contains("blockedWords"))
		r.blockedWords = list(json.value("blockedWords"));
	r.hideCommands = json.value("hideCommands").toBool(r.hideCommands);
	return r;
}

QJsonObject FilterRules::toJson() const
{
	return QJsonObject{{"hiddenUsers", QJsonArray::fromStringList(hiddenUsers)},
			   {"hideCommands", hideCommands},
			   {"blockedWords", QJsonArray::fromStringList(blockedWords)}};
}

QString foldText(const QString &text)
{
	const QString decomposed = text.normalized(QString::NormalizationForm_D);
	QString out;
	out.reserve(decomposed.size());
	for (const QChar c : decomposed)
		if (c.category() != QChar::Mark_NonSpacing && c.category() != QChar::Mark_Enclosing)
			out += c;
	return out.toCaseFolded();
}

namespace {

QString messageText(const QJsonObject &message)
{
	QString text;
	for (const QJsonValue v : message.value("parts").toArray()) {
		const QJsonObject p = v.toObject();
		// An emote counts as a word break, so "hey <emote> word" is still checked.
		text += p.value("type").toString() == QLatin1String("text") ? p.value("text").toString()
									      : QStringLiteral(" ");
	}
	return text;
}

bool containsWord(const QString &foldedText, const QString &foldedWord)
{
	// Whole word: no letter, digit or underscore right before or after the match.
	const QRegularExpression re(QStringLiteral("(?<![\\p{L}\\p{N}_])%1(?![\\p{L}\\p{N}_])")
					    .arg(QRegularExpression::escape(foldedWord)),
				    QRegularExpression::UseUnicodePropertiesOption);
	return re.match(foldedText).hasMatch();
}

} // namespace

bool isFiltered(const QJsonObject &message, const FilterRules &rules)
{
	const QString author = foldText(message.value("author").toString().trimmed());
	for (const QString &user : rules.hiddenUsers) {
		const QString hidden = foldText(user.trimmed());
		if (!hidden.isEmpty() && hidden == author)
			return true;
	}

	const QString text = messageText(message);
	if (rules.hideCommands && message.value("kind").toString() == QLatin1String("chat")) {
		static const QRegularExpression command(QStringLiteral("^\\s*!\\S"));
		if (command.match(text).hasMatch())
			return true;
	}

	if (!rules.blockedWords.isEmpty()) {
		const QString folded = foldText(text);
		for (const QString &word : rules.blockedWords) {
			const QString w = foldText(word.trimmed());
			if (!w.isEmpty() && containsWord(folded, w))
				return true;
		}
	}
	return false;
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
	if (platform == "starvios")
		return QStringLiteral("Starvios");
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
