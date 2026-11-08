#pragma once

// The dock's own preferences (dock.json next to config.json): text size, timestamps, hidden chats.

#include <QJsonObject>

namespace ui {

// The stored preferences; empty when there are none yet.
QJsonObject loadDockPrefs();
// Replaces dock.json; a failure is logged.
void saveDockPrefs(const QJsonObject &prefs);

} // namespace ui
