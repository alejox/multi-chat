#pragma once

// Nombre y foto de perfil de los canales, para la ventana de chats. Se piden en segundo plano
// y se guardan en memoria mientras OBS está abierto.

#include "../chat/platforms.hpp"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QThreadPool>

class ProfileCache : public QObject {
	Q_OBJECT

public:
	static ProfileCache &instance();

	// Si ya se consultó, lo devuelve en `out` (puede venir vacío si no se encontró) y da true;
	// si no, lo pide y emite ready(key) cuando llegue.
	bool get(const chat::SourceInfo &source, chat::Profile &out);
	void shutdown();

signals:
	void ready(const QString &key);

private:
	ProfileCache();

	QHash<QString, chat::Profile> profiles_;
	QSet<QString> pending_;
	QThreadPool pool_;
	bool shutdown_ = false;
};
