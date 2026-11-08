#include "i18n.hpp"

#include <obs-module.h>

namespace i18n {

QString tr(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

chat::Translator translator()
{
	return [](const QString &key) { return tr(key); };
}

} // namespace i18n
