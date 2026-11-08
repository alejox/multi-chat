#include "chat-history.hpp"

#include "chat/message.hpp"

void ChatHistory::append(const QJsonObject &message)
{
	messages_.append(message);
	while (messages_.size() > chat::kHistoryLimit)
		messages_.removeFirst();
}

QJsonObject ChatHistory::find(const QString &id) const
{
	if (id.isEmpty())
		return {};
	for (qsizetype i = messages_.size() - 1; i >= 0; --i) {
		const QJsonObject m = messages_.at(i).toObject();
		if (m.value("id").toString() == id)
			return m;
	}
	return {};
}

void ChatHistory::clear()
{
	cleared_ = messages_;
	messages_ = QJsonArray();
}

void ChatHistory::restore()
{
	// Messages that arrived after clearing stay, after the restored ones.
	QJsonArray merged = cleared_;
	for (const QJsonValue v : messages_)
		merged.append(v);
	while (merged.size() > chat::kHistoryLimit)
		merged.removeFirst();
	messages_ = merged;
}

QJsonArray ChatHistory::remove(const QJsonObject &deletion)
{
	QJsonArray ids;
	for (qsizetype i = messages_.size() - 1; i >= 0; --i) {
		const QJsonObject m = messages_.at(i).toObject();
		if (chat::deletionMatches(deletion, m)) {
			ids.append(m.value("id"));
			messages_.removeAt(i);
		}
	}
	// What "undo clear" would bring back must not include moderated messages either.
	for (qsizetype i = cleared_.size() - 1; i >= 0; --i)
		if (chat::deletionMatches(deletion, cleared_.at(i).toObject()))
			cleared_.removeAt(i);
	return ids;
}
