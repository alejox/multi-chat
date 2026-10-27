#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace ui {

// Texto traducido de data/locale/*.ini.
QString T(const char *key);

QColor platformColor(const QString &platform);
// Logo de la plataforma dibujado a mano (no depende de plugins de imagen de Qt).
QPixmap platformIcon(const QString &platform, int size, qreal dpr = 1.0);
// El logo con un punto de estado (verde conectado, ámbar conectando, rojo error).
QIcon statusIcon(const QString &platform, const QString &status, int size, qreal dpr);

// Etiqueta corta y color de fondo de una insignia (owner → HOST...).
bool badgeStyle(const QString &badge, QString &label, QColor &background);

// Aclara u oscurece el color del nombre para que se lea sobre `background`.
QColor readableColor(const QString &hex, const QString &name, const QColor &background);

} // namespace ui
