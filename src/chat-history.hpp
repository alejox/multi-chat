#pragma once

// The recent messages of every chat, newest last, with the copy that "undo clear" brings back.
// Owned by ChatManager on the main thread.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class ChatHistory {
public:
	const QJsonArray &messages() const { return messages_; }
	// Appends a message, dropping the oldest ones past chat::kHistoryLimit.
	void append(const QJsonObject &message);
	// The newest message with that id, or an empty object.
	QJsonObject find(const QString &id) const;

	// Empties the history, keeping it for restore().
	void clear();
	// True while restore() has something to bring back.
	bool canRestore() const { return !cleared_.isEmpty(); }
	// Puts the cleared messages back before the ones that arrived since, within the limit.
	void restore();
	void discardUndo() { cleared_ = QJsonArray(); }

	// Removes the messages a deletion event matches, from the history and from what restore()
	// would bring back. Returns the ids removed from the history, newest first.
	QJsonArray remove(const QJsonObject &deletion);

private:
	QJsonArray messages_;
	QJsonArray cleared_;
};
