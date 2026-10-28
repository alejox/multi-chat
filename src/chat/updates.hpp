#pragma once

// Comprobación de versiones nuevas en las Releases de GitHub.

#include <QJsonObject>
#include <QString>

namespace chat {

struct Release {
	QString version; // "2.0.2", sin la v
	QString url;     // zip de este sistema, o la página de la Release si no lo trae
	QString page;    // página de la Release
};

// true si `latest` es mayor que `current` (compara números: 2.0.10 > 2.0.9; ignora una v inicial).
bool isNewerVersion(const QString &latest, const QString &current);
// En la respuesta de /repos/<repo>/releases/latest; `assetSuffix` elige el zip del sistema.
Release parseLatestRelease(const QJsonObject &json, const QString &assetSuffix);
// Consulta la última Release. Bloquea (red). Versión vacía si no se pudo.
Release fetchLatestRelease(const QString &repo, const QString &assetSuffix);

} // namespace chat
