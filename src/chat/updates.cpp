#include "updates.hpp"
#include "../net/net.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QStringList>

namespace chat {

namespace {

QList<int> versionParts(QString v)
{
	v = v.trimmed();
	if (v.startsWith('v') || v.startsWith('V'))
		v.remove(0, 1);
	QList<int> parts;
	for (const QString &p : v.section('-', 0, 0).split('.'))
		parts << p.toInt();
	return parts;
}

bool isSha256Hex(const QString &hex)
{
	static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{64}$"));
	return re.match(hex).hasMatch();
}

} // namespace

QString sha256FromDigest(const QString &digest)
{
	const QString prefix = QStringLiteral("sha256:");
	if (!digest.startsWith(prefix, Qt::CaseInsensitive))
		return {};
	const QString hex = digest.mid(prefix.size()).trimmed();
	return isSha256Hex(hex) ? hex.toLower() : QString();
}

QString sha256FromSums(const QByteArray &sums, const QString &fileName)
{
	for (const QString &raw : QString::fromUtf8(sums).split(QLatin1Char('\n'))) {
		const QString line = raw.trimmed();
		const qsizetype space = line.indexOf(QLatin1Char(' '));
		if (space != 64 || !isSha256Hex(line.left(64)))
			continue;
		QString name = line.mid(space).trimmed();
		if (name.startsWith(QLatin1Char('*')))
			name.remove(0, 1);
		if (name == fileName)
			return line.left(64).toLower();
	}
	return {};
}

QString sha256OfFile(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file))
		return {};
	return QString::fromLatin1(hash.result().toHex());
}

bool verifySha256(const QByteArray &data, const QString &expectedHex)
{
	if (!isSha256Hex(expectedHex))
		return false;
	return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()) ==
	       expectedHex.toLower();
}

bool isNewerVersion(const QString &latest, const QString &current)
{
	const QList<int> a = versionParts(latest), b = versionParts(current);
	for (qsizetype i = 0; i < std::max(a.size(), b.size()); ++i) {
		const int x = a.value(i), y = b.value(i);
		if (x != y)
			return x > y;
	}
	return false;
}

Release parseLatestRelease(const QJsonObject &json, const QString &assetSuffix)
{
	Release r;
	if (json.value("draft").toBool() || json.value("prerelease").toBool())
		return r;
	r.version = json.value("tag_name").toString();
	if (r.version.startsWith('v'))
		r.version.remove(0, 1);
	r.page = json.value("html_url").toString();
	r.url = r.page;
	for (const QJsonValue a : json.value("assets").toArray()) {
		const QJsonObject asset = a.toObject();
		const QString name = asset.value("name").toString();
		if (name == QLatin1String("SHA256SUMS")) {
			r.sumsUrl = asset.value("browser_download_url").toString();
		} else if (r.assetName.isEmpty() && name.endsWith(assetSuffix)) {
			r.url = asset.value("browser_download_url").toString();
			r.assetName = name;
			r.sha256 = sha256FromDigest(asset.value("digest").toString());
		}
	}
	return r;
}

Release fetchLatestRelease(const QString &repo, const QString &assetSuffix)
{
	net::Response res = net::get(("https://api.github.com/repos/" + repo + "/releases/latest").toStdString(),
				     {{"Accept", "application/vnd.github+json"}});
	if (!res.ok())
		return {};
	return parseLatestRelease(QJsonDocument::fromJson(QByteArray::fromStdString(res.body)).object(), assetSuffix);
}

} // namespace chat
