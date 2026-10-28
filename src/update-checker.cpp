#include "update-checker.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QPointer>

namespace {

const char *const kRepo = "alejox/multi-chat";

#ifdef _WIN32
const char *const kAssetSuffix = "-windows-x64.zip";
#else
const char *const kAssetSuffix = "-macos.zip";
#endif

} // namespace

UpdateChecker &UpdateChecker::instance()
{
	static UpdateChecker *checker = new UpdateChecker(); // vive hasta que OBS cierra
	return *checker;
}

UpdateChecker::UpdateChecker()
{
	pool_.setMaxThreadCount(1);
	timer_.setInterval(12 * 60 * 60 * 1000);
	connect(&timer_, &QTimer::timeout, this, &UpdateChecker::check);
}

void UpdateChecker::start()
{
	// Unos segundos después de abrir OBS, para no competir con la carga de las escenas.
	QTimer::singleShot(15000, this, &UpdateChecker::check);
	timer_.start();
}

void UpdateChecker::check()
{
	if (shutdown_)
		return;
	QPointer<UpdateChecker> self(this);
	pool_.start([self] {
		const chat::Release release = chat::fetchLatestRelease(kRepo, kAssetSuffix);
		QMetaObject::invokeMethod(
			self,
			[self, release] {
				if (!self || release.version.isEmpty() ||
				    !chat::isNewerVersion(release.version, QString::fromLatin1(PLUGIN_VERSION)))
					return;
				obs_log(LOG_INFO, "hay una versión nueva: %s", release.version.toUtf8().constData());
				self->available_ = release;
				emit self->updateAvailable(release);
			},
			Qt::QueuedConnection);
	});
}

void UpdateChecker::shutdown()
{
	shutdown_ = true;
	timer_.stop();
	pool_.waitForDone();
}
