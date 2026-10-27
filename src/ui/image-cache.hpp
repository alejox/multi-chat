#pragma once

// Descarga y guarda en memoria las imágenes de los emotes para el dock.

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QThreadPool>

class ImageCache : public QObject {
	Q_OBJECT

public:
	static ImageCache &instance();

	// Devuelve la imagen si ya está; si no, la pide y emite ready() cuando llegue.
	QImage get(const QString &url);
	void shutdown();

signals:
	void ready(const QString &url);

private:
	ImageCache();

	QHash<QString, QImage> images_;
	QSet<QString> pending_;
	QSet<QString> failed_;
	QThreadPool pool_;
	bool shutdown_ = false;
};
