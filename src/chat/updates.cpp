#include "updates.hpp"
#include "../net/net.hpp"

#include <QJsonArray>
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

} // namespace

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
		if (asset.value("name").toString().endsWith(assetSuffix)) {
			r.url = asset.value("browser_download_url").toString();
			break;
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
