#include "config-store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

QJsonObject ConfigStore::load() const
{
	QFile file(path_);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return QJsonDocument::fromJson(file.readAll()).object();
}

bool ConfigStore::save(const QJsonObject &config) const
{
	if (path_.isEmpty())
		return false;
	QDir().mkpath(QFileInfo(path_).absolutePath());
	QFile file(path_);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;
	file.write(QJsonDocument(config).toJson());
	return true;
}
