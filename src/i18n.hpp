#pragma once

// The plugin's translated texts (data/locale/*.ini, through OBS). Always call it qualified,
// i18n::tr(...): inside a QObject an unqualified tr() is Qt's own, which knows nothing of these.

#include "chat/message.hpp"

#include <QString>

namespace i18n {

// The text for a locale key; OBS returns the key itself when it is missing.
QString tr(const char *key);
inline QString tr(const QString &key)
{
	return tr(key.toUtf8().constData());
}
// The same lookup for code that takes a translator (chat::translate, ChatManager).
chat::Translator translator();

} // namespace i18n
