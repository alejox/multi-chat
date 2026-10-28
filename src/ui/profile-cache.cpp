#include "profile-cache.hpp"

#include <QPointer>

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
