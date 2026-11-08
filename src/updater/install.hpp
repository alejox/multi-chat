#pragma once

// Per-OS installers of the updater (see updater.hpp). updater.cpp checks, downloads, verifies
// and unpacks the release; installPlugin() puts the unpacked plugin in place. Only the file for
// the current OS is compiled: install-win.cpp, install-mac.cpp or install-linux.cpp.

#include <QString>

namespace updater {

// Installs the plugin unpacked in `extracted`. On failure returns false with the reason, in
// OBS's language, in `problem`.
bool installPlugin(const QString &extracted, QString *problem);

// The loaded plugin binary, with its full path (defined in updater.cpp).
QString moduleBinary();
// Temporary folder of the update (defined in updater.cpp).
QString workDir();

#ifndef _WIN32
// Copies a folder tree over another one, creating what is missing and overwriting what exists
// (copy-tree.cpp).
bool copyTree(const QString &from, const QString &to, QString *problem);
#endif

} // namespace updater
