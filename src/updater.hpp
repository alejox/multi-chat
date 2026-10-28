#pragma once

// Actualizaciones del plugin desde las Releases de GitHub (como en music-now-playing).
//
// Al abrir OBS, cada 12 horas y desde Herramientas → "Multichat: buscar actualizaciones"
// revisa la última Release. Si trae una versión más nueva, pregunta (Actualizar ahora / Más
// tarde / Omitir esta versión) y el dock muestra un aviso con el botón "Actualizar".
// Al aceptar la instala sola: descarga el zip con el curl del sistema, lo descomprime (tar en
// Windows, ditto en macOS) y reemplaza el plugin; el cambio se aplica al reiniciar OBS.
//   - Windows: en C:\ProgramData\obs-studio\plugins\obs-multichat (carpeta de OBS 33). Es de
//     administrador: la copia la hace PowerShell con "Ejecutar como administrador". Si el plugin
//     estaba en una carpeta de antes, queda en la nueva y se quita la copia de antes.
//   - macOS: reemplaza obs-multichat.plugin y le quita la marca de descargado de internet.

#include "chat/updates.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

class QProgressDialog;

class Updater : public QObject {
	Q_OBJECT

public:
	static Updater &instance();

	// Revisa al abrir OBS y cada 12 horas; limpia lo que quedó de una actualización anterior.
	void start();
	void stop();
	// manual: también avisa si ya está al día o si falla la revisión.
	void check(bool manual);
	// Instala la versión encontrada (la del aviso del dock).
	void update();
	// La versión nueva encontrada (vacía si no hay).
	chat::Release available() const { return available_; }

signals:
	void updateAvailable(const chat::Release &release);

private:
	Updater();
	void handleRelease(const QByteArray &json, bool manual);
	void askToUpdate(const chat::Release &release);
	void download(const chat::Release &release);
	void install(const QString &version, const QString &zipPath);
	void fail(const QString &message, const QString &detail = QString());

	chat::Release available_;
	QString asked_; // versión ya preguntada en esta sesión de OBS
	QTimer timer_;
	bool busy_ = false;
	bool stopped_ = false;
	QProgressDialog *progress_ = nullptr;
};
