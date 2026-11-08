// macOS installer: replaces obs-multichat.plugin.

#include "install.hpp"
#include "../i18n.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace updater {

namespace {

// …/obs-multichat.plugin (el binario está en Contents/MacOS/).
QString bundleRoot()
{
	QDir dir = QFileInfo(moduleBinary()).absoluteDir();
	dir.cdUp();
	dir.cdUp();
	return dir.absolutePath();
}

} // namespace

// En macOS el binario cargado sigue funcionando aunque se reemplace el paquete.
bool installPlugin(const QString &extracted, QString *problem)
{
	const QString root = bundleRoot();
	const QString fresh = QDir(extracted).filePath("obs-multichat.plugin");
	if (QFileInfo(root).fileName() != "obs-multichat.plugin") {
		*problem = i18n::tr("Update.Problem.OwnFolder").arg(root);
		return false;
	}
	if (!QFileInfo(fresh).isDir()) {
		*problem = i18n::tr("Update.Problem.ZipNoBundle");
		return false;
	}
	const QString backup = root + ".old";
	QDir(backup).removeRecursively();
	if (!QDir().rename(root, backup)) {
		*problem = i18n::tr("Update.Problem.Move").arg(root);
		return false;
	}
	if (!QDir().rename(fresh, root) && !copyTree(fresh, root, problem)) {
		QDir(root).removeRecursively();
		QDir().rename(backup, root); // deja el plugin como estaba
		return false;
	}
	QDir(backup).removeRecursively();
	// Sin la marca de "descargado de internet", macOS no vuelve a pedir permiso para abrirlo.
	QProcess::execute("/usr/bin/xattr", {"-cr", root});
	return true;
}

} // namespace updater
