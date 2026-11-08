#include "profile-cache.hpp"

#include <QDateTime>
#include <QPointer>

namespace {
// A failed lookup (network, server) is reported for this long, then asked again.
constexpr qint64 kRetryMs = 15000;
} // namespace

ProfileCache &ProfileCache::instance()
{
	static ProfileCache *cache = new ProfileCache();
	return *cache;
}

ProfileCache::ProfileCache()
{
	pool_.setMaxThreadCount(2);
}

bool ProfileCache::get(const chat::SourceInfo &source, chat::Profile &out)
{
	auto it = profiles_.constFind(source.key);
	if (it != profiles_.constEnd()) {
		out = *it;
		return true;
	}
	const auto failed = failedAt_.constFind(source.key);
	if (failed != failedAt_.constEnd()) {
		if (QDateTime::currentMSecsSinceEpoch() - *failed < kRetryMs) {
			out = chat::Profile{};
			out.failed = true;
			return true;
		}
		failedAt_.erase(failed);
	}
	if (shutdown_ || pending_.contains(source.key) || source.platform == "demo")
		return false;
	pending_.insert(source.key);
	QPointer<ProfileCache> self(this);
	pool_.start([self, source] {
		chat::Profile profile = chat::fetchProfile(source);
		QMetaObject::invokeMethod(
			self,
			[self, source, profile] {
				if (!self)
					return;
				self->pending_.remove(source.key);
				if (profile.failed)
					self->failedAt_.insert(source.key, QDateTime::currentMSecsSinceEpoch());
				else
					self->profiles_.insert(source.key, profile);
				emit self->ready(source.key);
			},
			Qt::QueuedConnection);
	});
	return false;
}

void ProfileCache::shutdown()
{
	shutdown_ = true;
	pool_.waitForDone();
}
