#pragma once

// Convierte un mensaje de chat en el texto que se lee en voz alta. Solo depende de Qt Core
// para poder probarlo sin OBS (tests/test-tts.cpp).

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace tts {

struct SpeechOptions {
	bool includeAuthor = true;
	// Formato traducible con %1 (autor) y %2 (texto), por ejemplo "%1 dice: %2".
	QString authorFormat = QStringLiteral("%1: %2");
	// Solo se leen estas plataformas: un conjunto vacío no deja pasar ninguna.
	QSet<QString> allowedPlatforms;
	// Máximo de caracteres del contenido, sin contar el autor. 0 = sin límite.
	int maxLength = 200;
	// Una coincidencia (palabra completa, sin distinguir mayúsculas) en el autor que se lee o en el
	// contenido descarta todo el mensaje. El texto fijo de authorFormat no cuenta.
	QStringList blockedWords;
};

// Quita enlaces y emojis y deja un solo espacio entre palabras.
QString cleanSpeechText(const QString &text);

// Separa la lista de palabras bloqueadas (por comas o saltos de línea).
QStringList parseBlockedWords(const QString &text);

// Texto a leer, o vacío si el mensaje no se debe leer. El orden de lectura es:
//   - /me (action): "autor acción", sin el formato, porque el autor es el sujeto de la frase.
//   - Texto de sistema (suscripciones, regalos...): se lee tal cual y después el comentario, si
//     lo hay. Ya suele nombrar a la persona; si no la nombra, se antepone "autor, ".
//   - Mensaje normal: authorFormat con el autor y el texto.
QString speechTextForMessage(const QJsonObject &message, const SpeechOptions &options);

} // namespace tts
