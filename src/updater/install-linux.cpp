// Linux installer: replaces the plugin in ~/.config/obs-studio/plugins/obs-multichat.

#include "install.hpp"
#include "../i18n.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>

namespace updater {

// La carpeta de plugins del usuario: ~/.config/obs-studio/plugins/obs-multichat, con el .so en
// bin/64bit y los datos en data. Ahí OBS no pide permisos; si el plugin vino en un paquete del
// sistema (/usr/lib/…), lo actualiza ese paquete.
bool installPlugin(const QString &extracted, QString *problem)
{
	const QString binary = moduleBinary();
	QDir root = QFileInfo(binary).absoluteDir();
	const bool userFolder = root.dirName() == "64bit" && root.cdUp() && root.dirName() == "bin" && root.cdUp() &&
				root.dirName() == "obs-multichat";
	if (!userFolder || !QFileInfo(root.absolutePath()).isWritable()) {
		*problem = i18n::tr("Update.Problem.NotInLinux").arg(binary);
		return false;
	}
	const QString fresh = QDir(extracted).filePath("obs-multichat");
	const QString freshSo = fresh + "/bin/64bit/obs-multichat.so";
	if (!QFileInfo::exists(freshSo) || !QFileInfo(fresh + "/data").isDir()) {
		*problem = i18n::tr("Update.Problem.ArchiveNoFolder");
		return false;
	}
	// rename() cambia el archivo de una vez: el .so cargado sigue en memoria hasta reiniciar OBS.
	const QString temp = binary + ".new";
	QFile::remove(temp);
	if (!QFile::copy(freshSo, temp)) {
		*problem = i18n::tr("Update.Problem.CopyFile").arg(temp);
		return false;
	}
	if (std::rename(QFile::encodeName(temp).constData(), QFile::encodeName(binary).constData()) != 0) {
		QFile::remove(temp);
		*problem = i18n::tr("Update.Problem.ReplaceFile").arg(binary);
		return false;
	}
	return copyTree(fresh + "/data", root.filePath("data"), problem);
}

} // namespace updater
