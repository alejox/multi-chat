#pragma once

// Conectores y funciones de cada plataforma. Las funciones de parseo son puras para poder
// probarlas sin red (tests/).

#include "connector.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace chat {

// Estado en vivo: 1 en vivo, 0 offline, -1 no se sabe.
struct LiveResult {
	int live = -1;
	QString videoId; // YouTube: directo encontrado
};

// Busca el directo activo / consulta si el canal está en vivo. Bloquea (red).
LiveResult checkLive(const SourceInfo &source);

std::shared_ptr<Connector> createConnector(const SourceInfo &source, Connector::PostFn post,
					   Connector::ReportFn report);

// Interpreta "twitch:canal", "youtube:@canal", "kick:canal", "demo:demo" o una URL.
// Devuelve false y rellena `error` si no es válida.
bool parseEntry(const QString &entry, SourceInfo &out, QString &error);

namespace twitch {
struct IrcMessage {
	QHash<QString, QString> tags;
	QString prefix;
	QString command;
	QStringList params;
};
IrcMessage parseIrc(const QString &line);
QJsonArray splitEmotes(const QString &body, const QString &emotesTag);
// Devuelve un objeto vacío si la línea no es un mensaje de chat.
QJsonObject toMessage(const IrcMessage &msg, const QString &channel);
LiveResult checkLive(const QString &channel);
std::shared_ptr<Connector> create(const SourceInfo &source, Connector::PostFn post, Connector::ReportFn report);
} // namespace twitch

namespace kick {
QJsonArray splitContent(const QString &content);
QJsonObject toMessage(const QJsonObject &data, const QString &channel);
LiveResult checkLive(const QString &channel);
std::shared_ptr<Connector> create(const SourceInfo &source, Connector::PostFn post, Connector::ReportFn report);
} // namespace kick

namespace youtube {
QJsonObject toMessage(const QJsonObject &action, const QString &channel);
// Siguiente continuación del chat y cuánto esperar (ms).
QString nextContinuation(const QJsonObject &liveChatContinuation, int &timeoutMs);
// Busca el id del vídeo en una URL (watch?v=, youtu.be/, /live/).
QString parseVideoId(const QString &value);
// En el HTML de /@canal/live: id del directo si está en vivo.
QString liveVideoFromPage(const QString &html);
LiveResult checkLive(const SourceInfo &source);
std::shared_ptr<Connector> create(const SourceInfo &source, Connector::PostFn post, Connector::ReportFn report);
} // namespace youtube

namespace demo {
std::shared_ptr<Connector> create(const SourceInfo &source, Connector::PostFn post, Connector::ReportFn report);
} // namespace demo

} // namespace chat
