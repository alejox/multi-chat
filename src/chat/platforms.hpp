#pragma once

// Conectores y funciones de cada plataforma. Las funciones de parseo son puras para poder
// probarlas sin red (tests/).

#include "connector.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QList>
#include <QPair>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace chat {

// Estado en vivo: 1 en vivo, 0 offline, -1 no se sabe.
struct LiveResult {
	int live = -1;
	int viewers = -1; // espectadores ahora mismo, -1 si no se sabe
	QString videoId;  // YouTube: directo encontrado
};

// Busca el directo activo / consulta si el canal está en vivo. Bloquea (red).
LiveResult checkLive(const SourceInfo &source);

std::shared_ptr<Connector> createConnector(const SourceInfo &source, Connector::PostFn post,
					   Connector::ReportFn report);

// Interpreta "twitch:canal", "youtube:@canal", "kick:canal", "tiktok:@usuario", "demo:demo" o una URL.
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
// En el HTML de un directo: espectadores ahora mismo, o -1.
int viewersFromPage(const QString &html);
LiveResult checkLive(const SourceInfo &source);
std::shared_ptr<Connector> create(const SourceInfo &source, Connector::PostFn post, Connector::ReportFn report);
} // namespace youtube

namespace tiktok {
// Lector mínimo de protobuf (formato binario de TikTok LIVE).
class Proto {
public:
	explicit Proto(QByteArrayView data) : data_(data) {}
	// Avanza al siguiente campo. Devuelve false al terminar o si los datos no son válidos.
	bool next();
	int field() const { return field_; }
	quint64 varint() const { return varint_; }
	QByteArrayView bytes() const { return bytes_; }
	QString string() const { return QString::fromUtf8(bytes_); }

private:
	bool readVarint(quint64 &out);

	QByteArrayView data_;
	qsizetype pos_ = 0;
	int field_ = 0;
	quint64 varint_ = 0;
	QByteArrayView bytes_;
};

// Escritor mínimo de protobuf para los mensajes que enviamos.
class ProtoWriter {
public:
	ProtoWriter &varint(int field, quint64 value);
	ProtoWriter &bytes(int field, QByteArrayView value);
	QByteArray data() const { return out_; }

private:
	void raw(quint64 value);
	QByteArray out_;
};

// Un mensaje del directo (método + contenido protobuf).
struct Event {
	QString method;
	QByteArray payload;
};

struct FetchResult {
	QList<Event> events;
	QString internalExt;
	QString pushServer;
	QList<QPair<QString, QString>> routeParams;
	bool needAck = false;
};
FetchResult parseFetchResult(QByteArrayView data);

// Devuelve un objeto vacío si el evento no se muestra en el chat.
QJsonObject toMessage(const Event &event, const QString &channel);
// En la respuesta de /api-live/user/room/: id de la sala, si está en vivo y espectadores.
LiveResult parseRoom(const QJsonObject &response, QString &roomId);
LiveResult checkLive(const QString &channel);
std::shared_ptr<Connector> create(const SourceInfo &source, Connector::PostFn post, Connector::ReportFn report);
} // namespace tiktok

namespace demo {
std::shared_ptr<Connector> create(const SourceInfo &source, Connector::PostFn post, Connector::ReportFn report);
} // namespace demo

} // namespace chat
