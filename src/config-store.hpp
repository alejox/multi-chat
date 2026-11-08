#pragma once

// The plugin's JSON files on disk. Holds no OBS calls, so the chat manager can be built in tests
// with a temporary path.

#include <QJsonObject>
#include <QString>

// The object stored in a JSON file; empty when the file is missing or not a JSON object.
QJsonObject readJsonObject(const QString &path);
// Replaces the file with the object, creating its folder. The old file stays whole until the new
// one is complete (written to a temporary file and renamed). False when it could not be written.
bool writeJsonObject(const QString &path, const QJsonObject &object);

// config.json: the whole object, with a "version" for future migrations.
class ConfigStore {
public:
	// Format written by this version. Files without "version" are read as version 1.
	static constexpr int kVersion = 1;

	ConfigStore() = default;
	explicit ConfigStore(QString path) : path_(std::move(path)) {}

	const QString &path() const { return path_; }
	// The stored object, with "version" filled in; empty when the file is missing or not a JSON
	// object.
	QJsonObject load() const;
	// Writes the object with "version" = kVersion. False when the file could not be written.
	bool save(const QJsonObject &config) const;

private:
	QString path_;
};
