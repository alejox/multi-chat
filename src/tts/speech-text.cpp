#include "speech-text.hpp"

#include <QJsonArray>
#include <QRegularExpression>

namespace tts {

namespace {

// Bloques de Unicode donde están los emojis. Es a propósito más amplio que la lista oficial
// (incluye flechas y figuras): un sintetizador los lee en voz alta o los ignora, nunca aportan.
bool isEmoji(char32_t c)
{
	return (c >= 0x1F000 && c <= 0x1FBFF) || (c >= 0x2190 && c <= 0x21FF) || (c >= 0x2300 && c <= 0x23FF) ||
	       (c >= 0x25A0 && c <= 0x27BF) || (c >= 0x2900 && c <= 0x297F) || (c >= 0x2B00 && c <= 0x2BFF) ||
	       c == 0x203C || c == 0x2049 || c == 0x3030 || c == 0x303D || c == 0x3297 || c == 0x3299;
}

// Unen o modifican un emoji pero no separan palabras (ZWJ, selectores de variación, teclas).
bool isJoiner(char32_t c)
{
	return (c >= 0xFE00 && c <= 0xFE0F) || c == 0x20E3 || c == 0xFFFD ||
	       QChar::category(c) == QChar::Other_Format;
}

QString withoutEmoji(const QString &text)
{
	QString out;
	out.reserve(text.size());
	for (qsizetype i = 0; i < text.size(); ++i) {
		const QChar ch = text.at(i);
		char32_t code = ch.unicode();
		bool pair = false;
		if (ch.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
			code = QChar::surrogateToUcs4(ch, text.at(i + 1));
			pair = true;
		} else if (ch.isSurrogate()) {
			continue; // suelto: no es texto válido
		}
		if (isEmoji(code)) {
			out += QLatin1Char(' '); // "hola😀mundo" sigue siendo dos palabras
		} else if (!isJoiner(code)) {
			out += ch;
			if (pair)
				out += text.at(i + 1);
		}
		if (pair)
			++i;
	}
	return out;
}

bool hasLetterOrDigit(const QString &text)
{
	static const QRegularExpression letterOrDigit(QStringLiteral("[\\p{L}\\p{N}]"));
	return text.contains(letterOrDigit);
}

bool containsBlockedWord(const QString &text, const QStringList &words)
{
	for (const QString &word : words) {
		// Palabra completa también con letras que no son ASCII, y sin interpretar la palabra como regex.
		const QRegularExpression re(QStringLiteral("(?<![\\p{L}\\p{N}_])%1(?![\\p{L}\\p{N}_])")
						    .arg(QRegularExpression::escape(word.simplified())),
					    QRegularExpression::CaseInsensitiveOption |
						    QRegularExpression::UseUnicodePropertiesOption);
		if (re.match(text).hasMatch())
			return true;
	}
	return false;
}

// Corta en la última frontera de palabra sin pasarse del límite.
QString truncated(const QString &text, int maxLength)
{
	if (maxLength <= 0 || text.size() <= maxLength)
		return text;
	qsizetype cut = maxLength;
	if (text.at(cut).isLowSurrogate())
		--cut;
	const qsizetype space = text.lastIndexOf(QLatin1Char(' '), cut);
	if (space > 0)
		cut = space;
	return text.left(cut).trimmed();
}

// Texto de las partes "text"; los emotes solo separan palabras.
QString bodyText(const QJsonObject &message)
{
	QString body;
	const QJsonArray parts = message.value(QStringLiteral("parts")).toArray();
	for (const QJsonValue value : parts) {
		const QJsonObject part = value.toObject();
		if (part.value(QStringLiteral("type")).toString() == QStringLiteral("text"))
			body += part.value(QStringLiteral("text")).toString();
		else
			body += QLatin1Char(' ');
	}
	return body;
}

// Quita los :código: de los emotes de Starvios, que llegan dentro del texto y no se deben leer. Un
// código va separado del texto: "12:30:45" o "clave:valor:otro" no lo son.
QString withoutEmoteCodes(const QString &text)
{
	static const QRegularExpression code(
		QStringLiteral("(?<![\\p{L}\\p{N}_/]):[A-Za-z0-9_]{2,20}:(?![\\p{L}\\p{N}_/])"));
	QString out = text;
	return out.replace(code, QStringLiteral(" "));
}

QString spokenOrEmpty(const QString &text)
{
	const QString clean = cleanSpeechText(text);
	return hasLetterOrDigit(clean) ? clean : QString();
}

} // namespace

QString cleanSpeechText(const QString &text)
{
	static const QRegularExpression link(QStringLiteral("\\b(?:https?://|www\\.)\\S+"),
					     QRegularExpression::CaseInsensitiveOption);
	QString out = text;
	out.replace(link, QStringLiteral(" "));
	return withoutEmoji(out).simplified();
}

QStringList parseBlockedWords(const QString &text)
{
	static const QRegularExpression separator(QStringLiteral("[,\\n\\r]"));
	QStringList words;
	const QStringList pieces = text.split(separator, Qt::SkipEmptyParts);
	for (const QString &piece : pieces) {
		const QString word = piece.simplified();
		if (!word.isEmpty())
			words.append(word);
	}
	return words;
}

QString speechTextForMessage(const QJsonObject &message, const SpeechOptions &options)
{
	const QString platform = message.value(QStringLiteral("platform")).toString();
	if (!options.allowedPlatforms.contains(platform))
		return {};
	// Llegó tarde al reconectar: se muestra, pero leerlo ahora sonaría fuera de tiempo.
	if (message.value(QStringLiteral("recovered")).toBool())
		return {};

	const auto spokenPart = [&platform](const QString &text) {
		return spokenOrEmpty(platform == QStringLiteral("starvios") ? withoutEmoteCodes(text) : text);
	};
	const QString body = spokenPart(bodyText(message));
	const QString system = spokenPart(message.value(QStringLiteral("system")).toString());
	if (body.isEmpty() && system.isEmpty())
		return {};

	QString content = system;
	if (!body.isEmpty()) {
		if (!content.isEmpty() && !QStringLiteral(".!?").contains(content.back()))
			content += QLatin1Char('.');
		content += (content.isEmpty() ? QString() : QStringLiteral(" ")) + body;
	}

	const QString author =
		options.includeAuthor ? spokenOrEmpty(message.value(QStringLiteral("author")).toString()) : QString();
	const auto compose = [&](const QString &text) -> QString {
		if (author.isEmpty())
			return text;
		if (!system.isEmpty())
			return system.contains(author, Qt::CaseInsensitive) ? text : author + QStringLiteral(", ") + text;
		if (message.value(QStringLiteral("action")).toBool())
			return author + QLatin1Char(' ') + text;
		// arg() con dos argumentos sustituye a la vez: un "%2" en el nombre queda como texto.
		return options.authorFormat.arg(author, text);
	};

	// Las palabras bloqueadas se buscan en el autor que se va a leer y en todo el contenido, también
	// en lo que el corte dejaría fuera. Nunca en el texto fijo del formato: bloquear "dice" no puede
	// silenciar todos los mensajes.
	if (containsBlockedWord(author, options.blockedWords) || containsBlockedWord(content, options.blockedWords))
		return {};
	return compose(truncated(content, options.maxLength));
}

} // namespace tts
