#include "image-cache.hpp"
#include "../net/net.hpp"

#include <QPointer>

ImageCache &ImageCache::instance()
{
	static ImageCache *cache = new ImageCache();
	return *cache;
}

ImageCache::ImageCache()
{
	pool_.setMaxThreadCount(3);
}

QImage ImageCache::get(const QString &url)
{
	auto it = images_.constFind(url);
	if (it != images_.constEnd())
		return *it;
	if (shutdown_ || pending_.contains(url) || failed_.contains(url) || !url.startsWith("https://"))
		return {};

	if (images_.size() > 600)
		images_.clear();
	pending_.insert(url);
	QPointer<ImageCache> self(this);
	pool_.start([self, url] {
		net::Response res = net::get(url.toStdString(), {}, 8000);
		QImage image;
		if (res.ok())
			image.loadFromData(reinterpret_cast<const uchar *>(res.body.data()), int(res.body.size()));
		QMetaObject::invokeMethod(
			self,
			[self, url, image] {
				if (!self)
					return;
				self->pending_.remove(url);
				if (image.isNull()) {
					self->failed_.insert(url);
					return;
				}
				self->images_.insert(url, image);
				emit self->ready(url);
			},
			Qt::QueuedConnection);
	});
	return {};
}

void ImageCache::shutdown()
{
	shutdown_ = true;
	pool_.waitForDone();
}
