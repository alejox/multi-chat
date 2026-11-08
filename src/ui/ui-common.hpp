#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>

namespace ui {

// Name of the service in Settings → Stream ("Twitch", "YouTube - RTMPS", "Restream.io"...).
QString streamingServiceName();

// ---- Scenes (shared by the settings window and the first-run assistant) ----

enum class AddResult { Added, AlreadyThere, NoScene, Failed };
// Name of the scene being edited (the preview in studio mode); empty without scenes.
QString editedSceneName();
// Adds a source of type `id` to the edited scene, unless one is already there. A source of that
// type with the default name in another scene is reused (same settings, no duplicate names).
AddResult addToCurrentScene(const char *id, QString &sceneName);
// Adds the chosen Multichat sources (chat, featured message, voice) and returns one translated
// line per source saying what happened (Wizard.Sources.*), repeats removed.
QStringList addMultichatSources(bool chat, bool featured, bool voice);

QColor platformColor(const QString &platform);
// 950 · 1,2 K · 3,4 M
QString formatCount(int count);
// Logo de la plataforma dibujado a mano (no depende de plugins de imagen de Qt).
QPixmap platformIcon(const QString &platform, int size, qreal dpr = 1.0);
// Foto de perfil en círculo con el logo de la plataforma abajo a la derecha. Sin foto, el logo.
QPixmap avatarPixmap(const QImage &photo, const QString &platform, int size, qreal dpr);
// El logo con un punto de estado (verde conectado, ámbar conectando, rojo error).
QIcon statusIcon(const QString &platform, const QString &status, int size, qreal dpr);

// Etiqueta corta y color de fondo de una insignia (owner → HOST...).
bool badgeStyle(const QString &badge, QString &label, QColor &background);

// Aclara u oscurece el color del nombre para que se lea sobre `background`.
QColor readableColor(const QString &hex, const QString &name, const QColor &background);
// Red for errors and "on air", readable on `background`.
QColor alertColor(const QColor &background);
// `from` moved toward `to` by `t` (0..1).
QColor blend(const QColor &from, const QColor &to, float t);

// Monochrome icon from data/icons/<name>.svg painted in `color` (the theme's text color), with a
// faded copy for the disabled state. Empty if the file is missing.
QIcon themedIcon(const QString &name, const QColor &color, int size, qreal dpr);

} // namespace ui
