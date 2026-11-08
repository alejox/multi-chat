#include "dock-prefs.hpp"
#include "../config-store.hpp"

#include <obs-module.h>
#include <plugin-support.h>

namespace ui {

namespace {

QString prefsPath()
{
	char *path = obs_module_config_path("dock.json");
	QString out = QString::fromUtf8(path);
	bfree(path);
	return out;
}

} // namespace

QJsonObject loadDockPrefs()
{
	return readJsonObject(prefsPath());
}

void saveDockPrefs(const QJsonObject &prefs)
{
	const QString path = prefsPath();
	if (!writeJsonObject(path, prefs))
		obs_log(LOG_WARNING, "could not save %s", path.toUtf8().constData());
}

} // namespace ui
