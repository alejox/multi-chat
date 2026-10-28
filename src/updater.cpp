#include "updater.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

const char *const kLatestRelease = "https://api.github.com/repos/alejox/multi-chat/releases/latest";
const char *const kReleasesPage = "https://github.com/alejox/multi-chat/releases/latest";

#if defined(_WIN32)
const char *const kAssetSuffix = "-windows-x64.zip";
#elif defined(__APPLE__)
const char *const kAssetSuffix = "-macos.zip";
#else
const char *const kAssetSuffix = "-linux-x86_64.tar.gz";
#endif

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QWidget *mainWindow()
{
	return static_cast<QMainWindow *>(obs_frontend_get_main_window());
}

QString systemTool(const char *name)
{
#if defined(_WIN32)
	// curl.exe y tar.exe vienen con Windows 10 (1803) y 11.
	return qEnvironmentVariable("SystemRoot", "C:\\Windows") + "\\System32\\" + name + ".exe";
#elif defined(__APPLE__)
	return QString("/usr/bin/") + name;
#else
	const QString found = QStandardPaths::findExecutable(QString::fromLatin1(name));
	return found.isEmpty() ? QString("/usr/bin/") + name : found;
#endif
}

// La descarga con curl (y en Linux, si no está, con wget, que traen Ubuntu y Debian).
// file vacío: lo descargado sale por la salida estándar.
void downloadCommand(const QString &url, const QString &file, int seconds, bool api, QString *program,
		     QStringList *args)
{
#if !defined(_WIN32) && !defined(__APPLE__)
	if (QStandardPaths::findExecutable("curl").isEmpty() && !QStandardPaths::findExecutable("wget").isEmpty()) {
		*program = systemTool("wget");
		*args = {"-q", "--timeout", QString::number(seconds), "-O", file.isEmpty() ? QString("-") : file};
		if (api)
			*args << "--header" << "Accept: application/vnd.github+json";
		*args << url;
		return;
	}
#endif
	*program = systemTool("curl");
	*args = {"-fsSL", "--max-time", QString::number(seconds)};
	if (api)
		*args << "-H" << "Accept: application/vnd.github+json";
	if (!file.isEmpty())
		*args << "-o" << file;
	*args << url;
}

// "curl" o "wget", para los mensajes de error.
QString tool(const QProcess *process)
{
	return QFileInfo(process->program()).baseName();
}

QString workDir()
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("obs-multichat-update");
}

// La versión que el usuario pidió no volver a ofrecer.
QString skipFile()
{
	char *path = obs_module_config_path("skip-version.txt");
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

QString skippedVersion()
{
	QFile file(skipFile());
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

void skipVersion(const QString &version)
{
	QDir().mkpath(QFileInfo(skipFile()).absolutePath());
	QSaveFile file(skipFile());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(version.toUtf8());
		file.commit();
	}
}

// El binario cargado, con ruta completa (en la carpeta de OBS llega relativa a bin\64bit).
QString moduleBinary()
{
	return QDir::cleanPath(
		QFileInfo(QString::fromUtf8(obs_get_module_binary_path(obs_current_module()))).absoluteFilePath());
}

#ifdef _WIN32
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
		*problem = error == ERROR_CANCELLED ? text("Update.Cancelled")
						    : QString("No se pudo pedir permiso de administrador (error %1).").arg(error);
		return -1;
	}
	WaitForSingleObject(info.hProcess, 180000);
	DWORD code = 1;
	GetExitCodeProcess(info.hProcess, &code);
	CloseHandle(info.hProcess);
	return int(code);
}

// Copia el plugin nuevo a la carpeta de OBS 33 y quita las copias de antes. El .dll cargado no
// se puede borrar, pero sí renombrar: queda como .old (lo borra la actualización siguiente) y el
// nuevo se usa al reiniciar OBS. Si la copia falla, vuelve el .dll de antes.
const char *const kInstallScript = R"PS(param([string]$Dll, [string]$Data, [string]$Target, [string]$OldDll = '', [string]$OldData = '', [string]$OldBin = '')
$ErrorActionPreference = 'Stop'
$newDll = Join-Path $Target 'obs-multichat.dll'
$backup = ''
$copying = $false
try {
  New-Item -ItemType Directory -Force -Path $Target | Out-Null
  Get-ChildItem -LiteralPath $Target -Filter 'obs-multichat.dll.*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  if (Test-Path -LiteralPath $newDll) {
    $name = "obs-multichat.dll.$([DateTimeOffset]::Now.ToUnixTimeSeconds()).old"
    Rename-Item -LiteralPath $newDll -NewName $name
    $backup = Join-Path $Target $name
  }
  $copying = $true
  Copy-Item -LiteralPath $Dll -Destination $newDll -Force
  robocopy $Data (Join-Path $Target 'data') /E /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw "robocopy $LASTEXITCODE" }
} catch {
  if ($copying) { Remove-Item -LiteralPath $newDll -Force -ErrorAction SilentlyContinue }
  if ($backup) { Rename-Item -LiteralPath $backup -NewName 'obs-multichat.dll' -ErrorAction SilentlyContinue }
  exit 1
}
if ($OldDll -and (Test-Path -LiteralPath $OldDll)) {
  Get-ChildItem -LiteralPath (Split-Path $OldDll) -Filter 'obs-multichat.dll.*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  Rename-Item -LiteralPath $OldDll -NewName "obs-multichat.dll.$([DateTimeOffset]::Now.ToUnixTimeSeconds()).old" -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath ([IO.Path]::ChangeExtension($OldDll, '.pdb')) -Force -ErrorAction SilentlyContinue
}
if ($OldData -and (Test-Path -LiteralPath $OldData)) { Remove-Item -LiteralPath $OldData -Recurse -Force -ErrorAction SilentlyContinue }
if ($OldBin -and (Test-Path -LiteralPath $OldBin)) { Remove-Item -LiteralPath $OldBin -Recurse -Force -ErrorAction SilentlyContinue }
exit 0
)PS";

bool installWindows(const QString &extracted, QString *problem)
{
	const QString target = pluginFolder();
	const QString dll = QDir(extracted).filePath("obs-multichat/obs-multichat.dll");
	const QString data = QDir(extracted).filePath("obs-multichat/data");
	if (target.isEmpty()) {
		*problem = "No se encontró la carpeta ProgramData.";
		return false;
	}
	if (!QFileInfo::exists(dll) || !QFileInfo(data).isDir()) {
		*problem = "El zip no trae la carpeta obs-multichat.";
		return false;
	}
	const Location from = location();
	if (from == Location::Other) {
		*problem = QString("El plugin no está en una carpeta de plugins de OBS: %1")
				   .arg(QDir::toNativeSeparators(moduleBinary()));
		return false;
	}

	const QString script = QDir(workDir()).filePath("instalar.ps1");
	QFile scriptFile(script);
	if (!scriptFile.open(QIODevice::WriteOnly) || scriptFile.write(kInstallScript) < 0) {
		*problem = QString("No se pudo escribir %1").arg(QDir::toNativeSeparators(script));
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
		*problem = QString("No se pudo copiar el plugin a %1 (código %2).")
				   .arg(QDir::toNativeSeparators(target))
				   .arg(code);
	return code == 0;
}
#endif

#ifndef _WIN32
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
			*problem = QString("No se pudo reemplazar %1: %2").arg(target, existing.errorString());
			return false;
		}
		QFile copy(file);
		if (!copy.copy(target)) {
			*problem = QString("No se pudo copiar %1: %2").arg(target, copy.errorString());
			return false;
		}
	}
	return true;
}
#endif

#ifdef __APPLE__
// …/obs-multichat.plugin (el binario está en Contents/MacOS/).
QString bundleRoot()
{
	QDir dir = QFileInfo(moduleBinary()).absoluteDir();
	dir.cdUp();
	dir.cdUp();
	return dir.absolutePath();
}

// En macOS el binario cargado sigue funcionando aunque se reemplace el paquete.
bool installMac(const QString &extracted, QString *problem)
{
	const QString root = bundleRoot();
	const QString fresh = QDir(extracted).filePath("obs-multichat.plugin");
	if (QFileInfo(root).fileName() != "obs-multichat.plugin") {
		*problem = QString("El plugin no está en su propia carpeta: %1").arg(root);
		return false;
	}
	if (!QFileInfo(fresh).isDir()) {
		*problem = "El zip no trae obs-multichat.plugin.";
		return false;
	}
	const QString backup = root + ".old";
	QDir(backup).removeRecursively();
	if (!QDir().rename(root, backup)) {
		*problem = QString("No se pudo mover %1").arg(root);
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
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
// La carpeta de plugins del usuario: ~/.config/obs-studio/plugins/obs-multichat, con el .so en
// bin/64bit y los datos en data. Ahí OBS no pide permisos; si el plugin vino en un paquete del
// sistema (/usr/lib/…), lo actualiza ese paquete.
bool installLinux(const QString &extracted, QString *problem)
{
	const QString binary = moduleBinary();
	QDir root = QFileInfo(binary).absoluteDir();
	const bool userFolder = root.dirName() == "64bit" && root.cdUp() && root.dirName() == "bin" && root.cdUp() &&
				root.dirName() == "obs-multichat";
	if (!userFolder || !QFileInfo(root.absolutePath()).isWritable()) {
		*problem = QString("El plugin no está en ~/.config/obs-studio/plugins/obs-multichat: %1").arg(binary);
		return false;
	}
	const QString fresh = QDir(extracted).filePath("obs-multichat");
	const QString freshSo = fresh + "/bin/64bit/obs-multichat.so";
	if (!QFileInfo::exists(freshSo) || !QFileInfo(fresh + "/data").isDir()) {
		*problem = "El archivo no trae la carpeta obs-multichat.";
		return false;
	}
	// rename() cambia el archivo de una vez: el .so cargado sigue en memoria hasta reiniciar OBS.
	const QString temp = binary + ".new";
	QFile::remove(temp);
	if (!QFile::copy(freshSo, temp)) {
		*problem = QString("No se pudo copiar %1").arg(temp);
		return false;
	}
	if (std::rename(QFile::encodeName(temp).constData(), QFile::encodeName(binary).constData()) != 0) {
		QFile::remove(temp);
		*problem = QString("No se pudo reemplazar %1").arg(binary);
		return false;
	}
	return copyTree(fresh + "/data", root.filePath("data"), problem);
}
#endif

} // namespace

Updater &Updater::instance()
{
	static Updater *updater = new Updater(); // vive hasta que OBS cierra
	return *updater;
}

Updater::Updater()
{
	timer_.setInterval(12 * 60 * 60 * 1000);
	connect(&timer_, &QTimer::timeout, this, [this] { check(false); });
}

void Updater::start()
{
	QDir(workDir()).removeRecursively();
	// Unos segundos después de abrir OBS, para no competir con la carga de las escenas.
	QTimer::singleShot(10000, this, [this] { check(false); });
	timer_.start();
}

void Updater::stop()
{
	stopped_ = true;
	timer_.stop();
}

void Updater::check(bool manual)
{
	if (busy_ || stopped_)
		return;
	busy_ = true;
	auto *curl = new QProcess(this);
	connect(curl, &QProcess::finished, this, [this, curl, manual](int code, QProcess::ExitStatus) {
		const QByteArray json = curl->readAllStandardOutput();
		const QString error = QString::fromUtf8(curl->readAllStandardError()).trimmed();
		curl->deleteLater();
		busy_ = false;
		if (code != 0) {
			obs_log(LOG_INFO, "no se pudo revisar si hay actualizaciones (curl %d)", code);
			if (manual)
				fail(text("Update.CheckFailed"), QString("%1 %2: %3").arg(tool(curl), QString::number(code), error));
			return;
		}
		handleRelease(json, manual);
	});
	connect(curl, &QProcess::errorOccurred, this, [this, curl, manual](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart)
			return;
		curl->deleteLater();
		busy_ = false;
		if (manual)
			fail(text("Update.CheckFailed"), curl->errorString());
	});
	QString program;
	QStringList args;
	downloadCommand(QString::fromUtf8(kLatestRelease), QString(), 20, true, &program, &args);
	curl->start(program, args);
}

void Updater::handleRelease(const QByteArray &json, bool manual)
{
	const chat::Release release =
		chat::parseLatestRelease(QJsonDocument::fromJson(json).object(), QString::fromLatin1(kAssetSuffix));
	const QString current = QString::fromLatin1(PLUGIN_VERSION);
	if (release.version.isEmpty() || !release.url.endsWith(QLatin1String(kAssetSuffix))) {
		if (manual)
			fail(text("Update.CheckFailed"), QString("La Release no trae obs-multichat-…%1").arg(kAssetSuffix));
		return;
	}
	if (!chat::isNewerVersion(release.version, current)) {
		if (manual)
			QMessageBox::information(mainWindow(), text("Update.Title"), text("Update.UpToDate").arg(current));
		return;
	}
	obs_log(LOG_INFO, "hay una versión nueva: %s", release.version.toUtf8().constData());
	available_ = release;
	emit updateAvailable(release);
	// Se pregunta una vez por sesión de OBS (el aviso del dock queda), salvo que se omitiera.
	if (manual || (asked_ != release.version && skippedVersion() != release.version))
		askToUpdate(release);
}

void Updater::askToUpdate(const chat::Release &release)
{
	asked_ = release.version;
	QMessageBox box(mainWindow());
	box.setWindowTitle(text("Update.Title"));
	box.setIcon(QMessageBox::Information);
	box.setTextFormat(Qt::RichText);
	box.setTextInteractionFlags(Qt::TextBrowserInteraction);
	box.setText(text("Update.Ask").arg(release.version, QString::fromLatin1(PLUGIN_VERSION),
					   release.page.toHtmlEscaped()));
	QPushButton *now = box.addButton(text("Update.Now"), QMessageBox::AcceptRole);
	box.addButton(text("Update.Later"), QMessageBox::RejectRole);
	QPushButton *skip = box.addButton(text("Update.Skip"), QMessageBox::DestructiveRole);
	box.setDefaultButton(now);
	box.exec();
	if (box.clickedButton() == now)
		download(release);
	else if (box.clickedButton() == skip)
		skipVersion(release.version);
}

void Updater::update()
{
	if (!available_.version.isEmpty())
		download(available_);
}

void Updater::download(const chat::Release &release)
{
	if (busy_)
		return;
	busy_ = true;
	const QString dir = workDir();
	QDir(dir).removeRecursively();
	QDir().mkpath(dir);
	const QString zip = QDir(dir).filePath("plugin.zip");

	progress_ = new QProgressDialog(text("Update.Downloading").arg(release.version), QString(), 0, 0, mainWindow());
	progress_->setWindowTitle(text("Update.Title"));
	progress_->setWindowModality(Qt::WindowModal);
	progress_->setMinimumDuration(0);
	progress_->show();

	auto *curl = new QProcess(this);
	const QString version = release.version;
	connect(curl, &QProcess::finished, this, [this, curl, version, zip](int code, QProcess::ExitStatus) {
		const QString error = QString::fromUtf8(curl->readAllStandardError()).trimmed();
		curl->deleteLater();
		if (code != 0 || QFileInfo(zip).size() == 0) {
			busy_ = false;
			fail(text("Update.DownloadFailed"), QString("%1 %2: %3").arg(tool(curl), QString::number(code), error));
			return;
		}
		install(version, zip);
	});
	connect(curl, &QProcess::errorOccurred, this, [this, curl](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart)
			return;
		curl->deleteLater();
		busy_ = false;
		fail(text("Update.DownloadFailed"), curl->errorString());
	});
	QString program;
	QStringList args;
	downloadCommand(release.url, zip, 300, false, &program, &args);
	curl->start(program, args);
}

void Updater::install(const QString &version, const QString &zipPath)
{
	const QString extracted = QDir(workDir()).filePath("unzipped");
	QDir().mkpath(extracted);
#if defined(_WIN32)
	const int unzip = QProcess::execute(systemTool("tar"), {"-xf", zipPath, "-C", extracted});
#elif defined(__APPLE__)
	const int unzip = QProcess::execute(systemTool("ditto"), {"-x", "-k", zipPath, extracted});
#else
	const int unzip = QProcess::execute(systemTool("tar"), {"-xzf", zipPath, "-C", extracted});
#endif
	QString problem;
	bool ok = false;
	if (unzip != 0) {
		problem = QString("No se pudo descomprimir %1 (código %2)").arg(QDir::toNativeSeparators(zipPath)).arg(unzip);
	} else {
#if defined(_WIN32)
		ok = installWindows(extracted, &problem);
#elif defined(__APPLE__)
		ok = installMac(extracted, &problem);
#else
		ok = installLinux(extracted, &problem);
#endif
	}
	busy_ = false;
	if (progress_) {
		progress_->close();
		progress_->deleteLater();
		progress_ = nullptr;
	}
	if (!ok) {
		obs_log(LOG_WARNING, "no se pudo instalar la versión %s", version.toUtf8().constData());
		fail(text("Update.InstallFailed"), problem);
		return;
	}
	obs_log(LOG_INFO, "versión %s instalada; se usa al reiniciar OBS", version.toUtf8().constData());
	QMessageBox::information(mainWindow(), text("Update.Title"), text("Update.Done").arg(version));
}

void Updater::fail(const QString &message, const QString &detail)
{
	// El motivo va en el aviso y en el registro de OBS: sin él no se sabe qué falló.
	if (!detail.isEmpty())
		obs_log(LOG_WARNING, "actualización: %s", detail.toUtf8().constData());
	if (progress_) {
		progress_->close();
		progress_->deleteLater();
		progress_ = nullptr;
	}
	QMessageBox box(mainWindow());
	box.setWindowTitle(text("Update.Title"));
	box.setIcon(QMessageBox::Warning);
	box.setText(message);
	if (!detail.isEmpty())
		box.setInformativeText(detail);
	QPushButton *open = box.addButton(text("Update.OpenPage"), QMessageBox::AcceptRole);
	box.addButton(QMessageBox::Close);
	box.exec();
	if (box.clickedButton() == open)
		QDesktopServices::openUrl(QUrl(QString::fromLatin1(kReleasesPage)));
}
