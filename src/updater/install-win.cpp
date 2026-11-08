// Windows installer: copies the plugin to OBS 33's plugin folder with an elevated PowerShell.

#include "install.hpp"
#include "install-script-win.hpp"
#include "../i18n.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>

namespace updater {

namespace {

// La carpeta de OBS 33: C:\ProgramData\obs-studio\plugins\obs-multichat (vacío si no se sabe).
QString pluginFolder()
{
	QString programData = qEnvironmentVariable("ProgramData");
	if (programData.isEmpty())
		programData = qEnvironmentVariable("ALLUSERSPROFILE");
	return programData.isEmpty() ? QString() : QDir::cleanPath(programData + "/obs-studio/plugins/obs-multichat");
}

enum class Location { PluginFolder, LegacyObsFolder, LegacyProgramData, Other };

Location location()
{
	const QString binDir = QFileInfo(moduleBinary()).absolutePath();
	const QString target = pluginFolder();
	if (!target.isEmpty() && binDir.compare(target, Qt::CaseInsensitive) == 0)
		return Location::PluginFolder;
	if (!target.isEmpty() && binDir.compare(target + "/bin/64bit", Qt::CaseInsensitive) == 0)
		return Location::LegacyProgramData;
	QDir dir(binDir);
	if (dir.dirName() == "64bit" && QFileInfo(dir.absolutePath()).dir().dirName() == "obs-plugins")
		return Location::LegacyObsFolder;
	return Location::Other;
}

// Pide permiso de administrador y ejecuta el programa. Devuelve el código de salida, o -1 si no
// se pudo pedir el permiso (o se canceló).
int runElevated(const QString &program, const QString &params, QString *problem)
{
	const std::wstring file = program.toStdWString();
	const std::wstring args = params.toStdWString();
	SHELLEXECUTEINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	info.lpVerb = L"runas";
	info.lpFile = file.c_str();
	info.lpParameters = args.c_str();
	info.nShow = SW_HIDE;
	if (!ShellExecuteExW(&info) || !info.hProcess) {
		const DWORD error = GetLastError();
		*problem = error == ERROR_CANCELLED ? i18n::tr("Update.Cancelled")
						    : i18n::tr("Update.Problem.Elevate").arg(QString::number(error));
		return -1;
	}
	WaitForSingleObject(info.hProcess, 180000);
	DWORD code = 1;
	GetExitCodeProcess(info.hProcess, &code);
	CloseHandle(info.hProcess);
	return int(code);
}

} // namespace

bool installPlugin(const QString &extracted, QString *problem)
{
	const QString target = pluginFolder();
	const QString dll = QDir(extracted).filePath("obs-multichat/obs-multichat.dll");
	const QString data = QDir(extracted).filePath("obs-multichat/data");
	if (target.isEmpty()) {
		*problem = i18n::tr("Update.Problem.ProgramData");
		return false;
	}
	if (!QFileInfo::exists(dll) || !QFileInfo(data).isDir()) {
		*problem = i18n::tr("Update.Problem.ZipNoFolder");
		return false;
	}
	const Location from = location();
	if (from == Location::Other) {
		*problem = i18n::tr("Update.Problem.NotInPlugins").arg(QDir::toNativeSeparators(moduleBinary()));
		return false;
	}

	const QString script = QDir(workDir()).filePath("instalar.ps1");
	QFile scriptFile(script);
	if (!scriptFile.open(QIODevice::WriteOnly) || scriptFile.write(kInstallScript) < 0) {
		*problem = i18n::tr("Update.Problem.Write").arg(QDir::toNativeSeparators(script));
		return false;
	}
	scriptFile.close();

	auto arg = [](const char *name, const QString &value) {
		return QString(" -%1 \"%2\"").arg(QString::fromUtf8(name), QDir::toNativeSeparators(value));
	};
	QString params =
		QString("-NoProfile -ExecutionPolicy Bypass -File \"%1\"").arg(QDir::toNativeSeparators(script));
	params += arg("Dll", dll) + arg("Data", data) + arg("Target", target);
	// La copia de antes: el .dll cargado se renombra. Sus datos (que el overlay está usando) se
	// borran en la actualización siguiente, cuando el plugin ya corre desde la carpeta nueva.
	if (from != Location::PluginFolder)
		params += arg("OldDll", moduleBinary());
	const QString obsRoot = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../..");
	const QString oldData = obsRoot + "/data/obs-plugins/obs-multichat";
	if (from == Location::PluginFolder && QFileInfo(oldData).isDir())
		params += arg("OldData", oldData);
	if (QFileInfo(target + "/bin").isDir())
		params += arg("OldBin", target + "/bin");

	const int code = runElevated("powershell.exe", params, problem);
	if (code > 0)
		*problem = i18n::tr("Update.Problem.CopyTo").arg(QDir::toNativeSeparators(target), QString::number(code));
	return code == 0;
}

} // namespace updater
