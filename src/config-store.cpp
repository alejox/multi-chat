#include "config-store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

QJsonObject readJsonObject(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeJsonObject(const QString &path, const QJsonObject &object)
{
	if (path.isEmpty())
		return false;
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	const QByteArray data = QJsonDocument(object).toJson();
	if (file.write(data) != data.size()) {
		file.cancelWriting();
		return false;
	}
	return file.commit();
}

QJsonObject ConfigStore::load() const
{
	QJsonObject config = readJsonObject(path_);
	if (!config.isEmpty() && !config.contains("version"))
		config.insert("version", 1);
	return config;
}

bool ConfigStore::save(const QJsonObject &config) const
{
	QJsonObject versioned = config;
	versioned.insert("version", kVersion);
	return writeJsonObject(path_, versioned);
}
