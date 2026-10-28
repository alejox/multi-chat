#pragma once

// Avisa cuando hay una versión nueva del plugin en las Releases de GitHub: la comprueba al
// abrir OBS y cada 12 horas. El dock muestra el aviso con el botón de descarga.

#include "chat/updates.hpp"

#include <QObject>
#include <QThreadPool>
#include <QTimer>

class UpdateChecker : public QObject {
	Q_OBJECT

public:
	static UpdateChecker &instance();

	void start();
	void shutdown();
	// La versión nueva encontrada (vacía si no hay).
	chat::Release available() const { return available_; }

signals:
	void updateAvailable(const chat::Release &release);

private:
	UpdateChecker();
	void check();

	chat::Release available_;
	QTimer timer_;
	QThreadPool pool_;
	bool shutdown_ = false;
};
