// Shared by the macOS and Linux installers.

#include "install.hpp"
#include "../i18n.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace updater {

// Copia un árbol de carpetas encima de otro (crea lo que falte, pisa lo que exista).
bool copyTree(const QString &from, const QString &to, QString *problem)
{
	QDir source(from);
	QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QString file = it.next();
		const QString target = QDir(to).filePath(source.relativeFilePath(file));
		QDir().mkpath(QFileInfo(target).absolutePath());
		QFile existing(target);
		if (existing.exists() && !existing.remove()) {
			*problem = i18n::tr("Update.Problem.Replace").arg(target, existing.errorString());
			return false;
		}
		QFile copy(file);
		if (!copy.copy(target)) {
			*problem = i18n::tr("Update.Problem.Copy").arg(target, copy.errorString());
			return false;
		}
	}
	return true;
}

} // namespace updater
