#include "message.hpp"

#include <QDateTime>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace chat {

QString fillArgs(const QString &pattern, const QStringList &args)
{
	QString out;
	out.reserve(pattern.size());
	for (qsizetype i = 0; i < pattern.size(); ++i) {
		const QChar c = pattern.at(i);
		if (c == QLatin1Char('%') && i + 1 < pattern.size() && pattern.at(i + 1).isDigit()) {
			qsizetype end = i + 1;
			int number = 0;
			while (end < pattern.size() && pattern.at(end).isDigit() && end - i <= 2)
				number = number * 10 + pattern.at(end++).digitValue();
			if (number >= 1 && number <= args.size()) {
				out += args.at(number - 1);
				i = end - 1;
				continue;
			}
		}
		out += c;
	}
	return out;
}

const QHash<QString, QString> &englishTexts()
{
	// Keep in sync with data/locale/en-US.ini (tests/test-chat.cpp checks it).
	static const QHash<QString, QString> texts{
		{"Author.Anonymous", "Anonymous"},
		{"Author.Viewer", "Viewer"},
		{"Event.Raid", "%1 is raiding with %2 viewers"},
		{"Event.RaidOne", "%1 is raiding with 1 viewer"},
		{"Event.Reward", "%1 redeemed %2%3"},
		{"Event.RewardUnnamed", "%1 redeemed %2a reward"},
		{"Event.Sub", "%1 subscribed"},
		{"Event.SubMonths", "%1 subscribed for %2 months"},
		{"Event.GiftSub", "%1 gifted a sub"},
		{"Event.GiftSubTo", "%1 gifted a sub to %2"},
		{"Event.GiftSubAnonymous", "Anonymous gifted a sub"},
		{"Event.GiftSubAnonymousTo", "Anonymous gifted a sub to %1"},
		{"Event.PumpkinWin", "Congratulations, %1! They caught 10 pumpkins and got 100 Starvies"},
		{"Event.Gift", "%1 sent %2%3"},
		{"Event.GiftUnnamed", "%1 sent %2a gift"},
		{"Detail.WaitingLive", "waiting for the stream to start"},
		{"Error.Connect", "couldn't connect: %1"},
		{"Error.Responded", "%1 responded %2"},
		{"Error.Query", "couldn't reach %1 (%2)"},
		{"Error.Unexpected", "unexpected response from %1"},
		{"Error.Closed", "%1 closed the connection"},
		{"Error.Rejected", "%1 didn't accept the connection (something may have changed)"},
		{"Error.RejectedReason", "%1 rejected the connection (%2)"},
		{"Error.ChannelClosed", "%1 closed the chat channel"},
		{"Error.Reconnect", "%1 asked to reconnect"},
		{"Error.LiveEnded", "the live stream ended"},
		{"Error.LiveChatEnded", "the live chat ended"},
		{"Error.NoLiveChat", "the live stream has no chat available"},
		{"Error.YouTubeVideo", "couldn't open the YouTube video"},
		{"Error.TikTokNoUser", "the user doesn't exist or can't go live"},
		{"Error.SignLimit", "Euler Stream (TikTok signing): too many connections, retrying in 10 min"},
		{"Error.SignResponded", "Euler Stream (TikTok signing) responded %1"},
		{"Error.SignNoServer", "Euler Stream didn't return the TikTok server"},
		{"Error.Compressed", "TikTok sent compressed messages (%1)"},
		{"Error.KickChannel", "couldn't look up the Kick channel (%1). Enter the chat by hand: kick:%2#ID"},
		{"Error.KickNoChat", "couldn't find the Kick channel's chat. Enter it by hand: kick:%1#ID"},
		{"Error.Pusher", "Pusher %1: %2"},
		{"Error.Suspended", "channel suspended or doesn't exist"},
	};
	return texts;
}

QString translate(const Text &text, const Translator &translator)
{
	if (text.key.isEmpty())
		return text.args.value(0);
	QString pattern = translator ? translator(text.key) : QString();
	if (pattern.isEmpty() || pattern == text.key)
		pattern = englishTexts().value(text.key, text.key);
	return fillArgs(pattern, text.args);
}

void localizeMessage(QJsonObject &message, const Translator &translator)
{
	const QString systemKey = message.value("systemKey").toString();
	if (!systemKey.isEmpty()) {
		QStringList args;
		for (const QJsonValue v : message.value("systemArgs").toArray())
			args << v.toString();
		message.insert("system", translate(Text{systemKey, args}, translator));
	}
	const QString authorKey = message.value("authorKey").toString();
	if (!authorKey.isEmpty())
		message.insert("author", translate(Text{authorKey, {}}, translator));
}

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
	if (!o.systemText.isEmpty()) {
		m.insert("systemKey", o.systemText.key);
		m.insert("systemArgs", QJsonArray::fromStringList(o.systemText.args));
		m.insert("system", translate(o.systemText));
	} else if (!o.system.isEmpty()) {
		m.insert("system", o.system);
	}
	if (!o.authorKey.isEmpty())
		m.insert("authorKey", o.authorKey);
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

QJsonObject hideMessage(const QJsonObject &message)
{
	const QString source = message.value("source").toString();
	const QString id = message.value("id").toString();
	if (source.isEmpty() || id.isEmpty())
		return {};
	QJsonObject e = deleteMessage(message.value("platform").toString(), id);
	e.insert("source", source);
	return e;
}

QJsonObject hideAuthor(const QJsonObject &message)
{
	const QString source = message.value("source").toString();
	const QString author = message.value("author").toString();
	const QString authorId = message.value("authorId").toString();
	if (source.isEmpty() || (author.isEmpty() && authorId.isEmpty()))
		return {};
	QJsonObject e = deleteAuthor(message.value("platform").toString(), authorId, author);
	e.insert("source", source);
	return e;
}

QString messageText(const QJsonObject &message)
{
	QString text;
	for (const QJsonValue v : message.value("parts").toArray()) {
		const QJsonObject p = v.toObject();
		text += p.value("type").toString() == QLatin1String("emote") ? p.value("name").toString()
									       : p.value("text").toString();
	}
	return text;
}

QString plainText(const QJsonObject &message)
{
	QString text = messageText(message);
	if (text.trimmed().isEmpty())
		text = message.value("system").toString();
	const QString author = message.value("author").toString();
	if (author.isEmpty())
		return text;
	if (text.isEmpty())
		return author;
	return author + (message.value("action").toBool() ? QStringLiteral(" ") : QStringLiteral(": ")) + text;
}

bool matchesSearch(const QJsonObject &message, const QString &query)
{
	const QStringList words = foldText(query).simplified().split(QChar(' '), Qt::SkipEmptyParts);
	if (words.isEmpty())
		return true;
	const QString haystack = foldText(message.value("author").toString() + QChar('\n') + messageText(message) +
					  QChar('\n') + message.value("system").toString());
	return std::all_of(words.cbegin(), words.cend(), [&haystack](const QString &w) { return haystack.contains(w); });
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

QString filterText(const QJsonObject &message)
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

	const QString text = filterText(message);
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
