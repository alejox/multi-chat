#include "featured-state.hpp"

void FeaturedState::show(const QJsonObject &message, qint64 now)
{
	current_ = message;
	last_ = message;
	changedAt_ = now;
}

bool FeaturedState::hide(qint64 now)
{
	if (current_.isEmpty())
		return false;
	current_ = QJsonObject();
	changedAt_ = now;
	return true;
}

void FeaturedState::stashLast()
{
	stashedLast_ = last_;
	last_ = QJsonObject();
}

void FeaturedState::restoreLast()
{
	if (last_.isEmpty())
		last_ = stashedLast_;
}

bool FeaturedState::forget(const QJsonObject &deletion)
{
	if (!stashedLast_.isEmpty() && chat::deletionMatches(deletion, stashedLast_))
		stashedLast_ = QJsonObject();
	if (!last_.isEmpty() && chat::deletionMatches(deletion, last_))
		last_ = QJsonObject();
	return !current_.isEmpty() && chat::deletionMatches(deletion, current_);
}

void FeaturedState::setPolicy(const QString &sourceId, const chat::FeaturedHidePolicy &policy)
{
	policies_.insert(sourceId, policy);
}

void FeaturedState::removePolicy(const QString &sourceId)
{
	policies_.remove(sourceId);
}

std::optional<qint64> FeaturedState::remainingMs(qint64 now) const
{
	if (current_.isEmpty())
		return std::nullopt;
	const int duration = chat::featuredDurationMs(chat::featuredTextLength(current_), policies_.values());
	if (duration < 0)
		return std::nullopt;
	return changedAt_ + duration - now;
}
