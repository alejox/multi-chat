#pragma once

// Comprobación de versiones nuevas en las Releases de GitHub.

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace chat {

struct Release {
	QString version; // "2.0.2", sin la v
	QString url;     // zip de este sistema, o la página de la Release si no lo trae
	QString page;    // página de la Release
	QString assetName; // file name of this system's archive (empty if the release lacks it)
	QString sha256;    // its SHA-256 from the asset's "digest" (lowercase hex), empty if absent
	QString sumsUrl;   // the release's SHA256SUMS asset, empty if absent
};

// "sha256:<64 hex>" (GitHub's asset digest) → lowercase hex; empty if it is not a SHA-256 digest.
QString sha256FromDigest(const QString &digest);
// The hash of `fileName` in a SHA256SUMS file ("<hex>  <name>" or "<hex> *<name>" per line),
// lowercase; empty if the file is not listed.
QString sha256FromSums(const QByteArray &sums, const QString &fileName);
// Lowercase hex SHA-256 of a file; empty if it cannot be read.
QString sha256OfFile(const QString &path);
// True if `data` hashes to `expectedHex` (any case). An empty expected hash never matches.
bool verifySha256(const QByteArray &data, const QString &expectedHex);

// true si `latest` es mayor que `current` (compara números: 2.0.10 > 2.0.9; ignora una v inicial).
bool isNewerVersion(const QString &latest, const QString &current);
// En la respuesta de /repos/<repo>/releases/latest; `assetSuffix` elige el zip del sistema.
Release parseLatestRelease(const QJsonObject &json, const QString &assetSuffix);
// Consulta la última Release. Bloquea (red). Versión vacía si no se pudo.
Release fetchLatestRelease(const QString &repo, const QString &assetSuffix);

} // namespace chat
