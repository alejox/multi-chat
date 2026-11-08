#pragma once

// The featured message (the one "Multichat (featured)" shows large): what is on screen, the last
// one shown (for "show the last one again"), its copy for "undo clear" and the auto-hide policies
// of the featured sources. Pure state: ChatManager owns the timer and the signals.

#include "chat/message.hpp"

#include <QHash>
#include <QJsonObject>
#include <QString>

#include <optional>

class FeaturedState {
public:
	// Empty when nothing is featured.
	const QJsonObject &current() const { return current_; }
	// When it was featured or removed, in ms since the epoch.
	qint64 changedAt() const { return changedAt_; }
	// The last featured message, even if it was removed; empty after clear or moderation.
	const QJsonObject &last() const { return last_; }

	void show(const QJsonObject &message, qint64 now);
	// Removes the message on screen. False if there was none.
	bool hide(qint64 now);

	// Clearing the chat forgets the last one; undoing the clear brings it back if none came since.
	void stashLast();
	void restoreLast();
	void discardStash() { stashedLast_ = QJsonObject(); }

	// A moderated message cannot come back with "show the last one again" nor with undo. Returns
	// true when the message on screen matches (the caller hides it).
	bool forget(const QJsonObject &deletion);

	void setPolicy(const QString &sourceId, const chat::FeaturedHidePolicy &policy);
	void removePolicy(const QString &sourceId);
	// Milliseconds the message on screen has left, counted from changedAt() so a policy that
	// changes shortens or extends it instead of restarting it; zero or less = it is due.
	// Empty when nothing is featured or no policy hides it.
	std::optional<qint64> remainingMs(qint64 now) const;

private:
	QJsonObject current_;
	QJsonObject last_;
	QJsonObject stashedLast_;
	qint64 changedAt_ = 0;
	QHash<QString, chat::FeaturedHidePolicy> policies_; // by featured source id
};
