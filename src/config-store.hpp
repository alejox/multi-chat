#pragma once

// The plugin's config.json on disk: reads and writes the whole JSON object. Holds no OBS calls, so
// the chat manager can be built in tests with a temporary path.

#include <QJsonObject>
#include <QString>

class ConfigStore {
public:
	ConfigStore() = default;
	explicit ConfigStore(QString path) : path_(std::move(path)) {}

	const QString &path() const { return path_; }
	// The stored object; empty when the file is missing or not a JSON object.
	QJsonObject load() const;
	// Writes the object, creating the folder. False when the file could not be written.
	bool save(const QJsonObject &config) const;

private:
	QString path_;
};
