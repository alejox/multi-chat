#include "featured-state.hpp"

#include <algorithm>

void FeaturedState::show(const QJsonObject &message, qint64 now)
{
	current_ = message;
	last_ = message;
	changedAt_ = now;
	pausedSince_ = 0;
	pausedTotal_ = 0;
}

bool FeaturedState::setPaused(bool paused, qint64 now)
{
	if (current_.isEmpty() || paused == this->paused())
		return false;
	if (paused) {
		pausedSince_ = std::max<qint64>(now, 1);
	} else {
		pausedTotal_ += now - pausedSince_;
		pausedSince_ = 0;
	}
	return true;
}

bool FeaturedState::hide(qint64 now)
{
	if (current_.isEmpty())
		return false;
	current_ = QJsonObject();
	changedAt_ = now;
	pausedSince_ = 0;
	pausedTotal_ = 0;
	return true;
}

void FeaturedState::stashLast()
{
	// A second clear with nothing featured since keeps the one from before the first.
	if (!last_.isEmpty())
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
	if (current_.isEmpty() || paused())
		return std::nullopt;
	const int duration = chat::featuredDurationMs(chat::featuredTextLength(current_), policies_.values());
	if (duration < 0)
		return std::nullopt;
	return changedAt_ + pausedTotal_ + duration - now;
}
