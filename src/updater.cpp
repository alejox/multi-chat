#include "updater.hpp"
#include "i18n.hpp"
#include "updater/install.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QDesktopServices>
#include <QDir>
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

} // namespace

QString updater::workDir()
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("obs-multichat-update");
}

namespace {

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

} // namespace

// El binario cargado, con ruta completa (en la carpeta de OBS llega relativa a bin\64bit).
QString updater::moduleBinary()
{
	return QDir::cleanPath(
		QFileInfo(QString::fromUtf8(obs_get_module_binary_path(obs_current_module()))).absoluteFilePath());
}

using updater::workDir;

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
			obs_log(LOG_INFO, "could not check for updates (curl %d)", code);
			if (manual)
				fail(i18n::tr("Update.CheckFailed"), QString("%1 %2: %3").arg(tool(curl), QString::number(code), error));
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
			fail(i18n::tr("Update.CheckFailed"), curl->errorString());
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
			fail(i18n::tr("Update.CheckFailed"), i18n::tr("Update.Problem.NoAsset").arg(QString::fromLatin1(kAssetSuffix)));
		return;
	}
	if (!chat::isNewerVersion(release.version, current)) {
		if (manual)
			QMessageBox::information(mainWindow(), i18n::tr("Update.Title"), i18n::tr("Update.UpToDate").arg(current));
		return;
	}
	obs_log(LOG_INFO, "a new version is available: %s", release.version.toUtf8().constData());
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
	box.setWindowTitle(i18n::tr("Update.Title"));
	box.setIcon(QMessageBox::Information);
	box.setTextFormat(Qt::RichText);
	box.setTextInteractionFlags(Qt::TextBrowserInteraction);
	box.setText(i18n::tr("Update.Ask").arg(release.version, QString::fromLatin1(PLUGIN_VERSION),
					   release.page.toHtmlEscaped()));
	QPushButton *now = box.addButton(i18n::tr("Update.Now"), QMessageBox::AcceptRole);
	box.addButton(i18n::tr("Update.Later"), QMessageBox::RejectRole);
	QPushButton *skip = box.addButton(i18n::tr("Update.Skip"), QMessageBox::DestructiveRole);
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

	progress_ = new QProgressDialog(i18n::tr("Update.Downloading").arg(release.version), QString(), 0, 0, mainWindow());
	progress_->setWindowTitle(i18n::tr("Update.Title"));
	progress_->setWindowModality(Qt::WindowModal);
	progress_->setMinimumDuration(0);
	progress_->show();

	auto *curl = new QProcess(this);
	connect(curl, &QProcess::finished, this, [this, curl, release, zip](int code, QProcess::ExitStatus) {
		const QString error = QString::fromUtf8(curl->readAllStandardError()).trimmed();
		curl->deleteLater();
		if (code != 0 || QFileInfo(zip).size() == 0) {
			busy_ = false;
			fail(i18n::tr("Update.DownloadFailed"), QString("%1 %2: %3").arg(tool(curl), QString::number(code), error));
			return;
		}
		verify(release, zip);
	});
	connect(curl, &QProcess::errorOccurred, this, [this, curl](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart)
			return;
		curl->deleteLater();
		busy_ = false;
		fail(i18n::tr("Update.DownloadFailed"), curl->errorString());
	});
	QString program;
	QStringList args;
	downloadCommand(release.url, zip, 300, false, &program, &args);
	curl->start(program, args);
}

// The expected hash comes from the asset's "digest" in the GitHub API; releases without it fall
// back to the SHA256SUMS file that release.yml publishes. Without either, nothing is installed.
void Updater::verify(const chat::Release &release, const QString &zipPath)
{
	if (!release.sha256.isEmpty()) {
		installIfMatches(release.version, zipPath, release.sha256);
		return;
	}
	if (release.sumsUrl.isEmpty() || release.assetName.isEmpty()) {
		busy_ = false;
		fail(i18n::tr("Update.NoChecksum"), i18n::tr("Update.Problem.NoSums"));
		return;
	}
	const QString sums = QDir(workDir()).filePath("SHA256SUMS");
	auto *curl = new QProcess(this);
	connect(curl, &QProcess::finished, this, [this, curl, release, zipPath, sums](int code, QProcess::ExitStatus) {
		const QString error = QString::fromUtf8(curl->readAllStandardError()).trimmed();
		curl->deleteLater();
		QFile file(sums);
		if (code != 0 || !file.open(QIODevice::ReadOnly)) {
			busy_ = false;
			fail(i18n::tr("Update.NoChecksum"), QString("%1 %2: %3").arg(tool(curl), QString::number(code), error));
			return;
		}
		installIfMatches(release.version, zipPath, chat::sha256FromSums(file.readAll(), release.assetName));
	});
	connect(curl, &QProcess::errorOccurred, this, [this, curl](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart)
			return;
		curl->deleteLater();
		busy_ = false;
		fail(i18n::tr("Update.NoChecksum"), curl->errorString());
	});
	QString program;
	QStringList args;
	downloadCommand(release.sumsUrl, sums, 30, false, &program, &args);
	curl->start(program, args);
}

void Updater::installIfMatches(const QString &version, const QString &zipPath, const QString &expected)
{
	if (expected.isEmpty()) {
		busy_ = false;
		fail(i18n::tr("Update.NoChecksum"), i18n::tr("Update.Problem.NotListed"));
		return;
	}
	const QString actual = chat::sha256OfFile(zipPath);
	if (actual != expected) {
		busy_ = false;
		obs_log(LOG_WARNING, "update %s: SHA-256 mismatch (expected %s, got %s)", version.toUtf8().constData(),
			expected.toUtf8().constData(), actual.toUtf8().constData());
		fail(i18n::tr("Update.VerifyFailed"), i18n::tr("Update.Problem.Hash").arg(expected, actual));
		return;
	}
	obs_log(LOG_INFO, "update %s: SHA-256 verified", version.toUtf8().constData());
	install(version, zipPath);
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
		problem = i18n::tr("Update.Problem.Unzip").arg(QDir::toNativeSeparators(zipPath), QString::number(unzip));
	} else {
		ok = updater::installPlugin(extracted, &problem);
	}
	busy_ = false;
	if (progress_) {
		progress_->close();
		progress_->deleteLater();
		progress_ = nullptr;
	}
	if (!ok) {
		obs_log(LOG_WARNING, "could not install version %s", version.toUtf8().constData());
		fail(i18n::tr("Update.InstallFailed"), problem);
		return;
	}
	obs_log(LOG_INFO, "version %s installed; it is used after restarting OBS", version.toUtf8().constData());
	QMessageBox::information(mainWindow(), i18n::tr("Update.Title"), i18n::tr("Update.Done").arg(version));
}

void Updater::fail(const QString &message, const QString &detail)
{
	// El motivo va en el aviso y en el registro de OBS: sin él no se sabe qué falló.
	if (!detail.isEmpty())
		obs_log(LOG_WARNING, "update: %s", detail.toUtf8().constData());
	if (progress_) {
		progress_->close();
		progress_->deleteLater();
		progress_ = nullptr;
	}
	QMessageBox box(mainWindow());
	box.setWindowTitle(i18n::tr("Update.Title"));
	box.setIcon(QMessageBox::Warning);
	box.setText(message);
	if (!detail.isEmpty())
		box.setInformativeText(detail);
	QPushButton *open = box.addButton(i18n::tr("Update.OpenPage"), QMessageBox::AcceptRole);
	box.addButton(QMessageBox::Close);
	box.exec();
	if (box.clickedButton() == open)
		QDesktopServices::openUrl(QUrl(QString::fromLatin1(kReleasesPage)));
}
