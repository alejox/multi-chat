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

#include <vector>

namespace chat {

// Estado en vivo: 1 en vivo, 0 offline, -1 no se sabe.
struct LiveResult {
	int live = -1;
	int viewers = -1; // espectadores ahora mismo, -1 si no se sabe
	QString videoId;  // YouTube: directo encontrado
};

// Busca el directo activo / consulta si el canal está en vivo. Bloquea (red).
LiveResult checkLive(const SourceInfo &source);

// Nombre visible y foto de perfil de un canal (vacíos si no se pudo saber).
struct Profile {
	QString name;
	QString avatar; // URL https de la imagen
};
// Consulta el perfil del canal. Bloquea (red).
Profile fetchProfile(const SourceInfo &source);

std::shared_ptr<Connector> createConnector(const SourceInfo &source, Connector::PostFn post,
					   Connector::ReportFn report);

// Why an entry is not valid. The UI translates it with entryErrorKey(); `arg` in parseEntry is the
// offending text (the whole entry, the platform word or the user name).
enum class EntryError { None, Empty, Format, Platform, TikTokUser, StarviosUser, YouTubeUrl, StarviosUrl };
// "Entry.Error.<Name>" in data/locale; nullptr for None. The text has a %1 for `arg`.
const char *entryErrorKey(EntryError error);

// Interpreta "twitch:canal", "youtube:@canal", "kick:canal", "tiktok:@usuario", "starvios:usuario", "demo:demo" o una URL.
// Devuelve false y rellena `error` si no es válida.
bool parseEntry(const QString &entry, SourceInfo &out, EntryError &error, QString &arg);
// Same, with an untranslated English explanation (logs and tests).
bool parseEntry(const QString &entry, SourceInfo &out, QString &error);

// Text typed in the settings dialog as an entry: kept as is when it is a URL or starts with a
// platform word and ":", otherwise prefixed with `platform`. Empty for blank text.
QString entryFromInput(const QString &text, const QString &platform);
// The test chat ("demo:demo", added by the "test messages" option).
inline bool isDemo(const SourceInfo &source)
{
	return source.platform == QLatin1String("demo");
}

// A configured chat. Paused chats (enabled = false) stay in the list but are not connected.
struct ChatEntry {
	QString entry;
	bool enabled = true;
	bool operator==(const ChatEntry &other) const { return entry == other.entry && enabled == other.enabled; }
	bool operator!=(const ChatEntry &other) const { return !(*this == other); }
};
// config.json: "chats" holds the entries in the user's order (strings, as earlier versions wrote
// them; objects {"entry", "enabled"} are also read) and "disabled" the paused ones. Missing
// "enabled" means enabled. Writing only touches those two keys, and drops "disabled" when empty.
QList<ChatEntry> chatEntriesFromJson(const QJsonObject &config);
void chatEntriesToJson(const QList<ChatEntry> &entries, QJsonObject &config);

struct EntryProblem {
	EntryError code = EntryError::None;
	QString arg;
};
// The chats to connect: enabled, valid, without repeats, in the user's order, with the test chat
// last when `demo`. `errors` lists the invalid entries (paused ones included).
struct SourcePlan {
	std::vector<SourceInfo> sources;
	QList<EntryProblem> errors;
};
SourcePlan planSources(const QList<ChatEntry> &entries, bool demo);

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
// CLEARMSG / CLEARCHAT as a moderation event (see message.hpp); empty for anything else.
QJsonObject toDeletion(const IrcMessage &msg);
LiveResult checkLive(const QString &channel);
// En la respuesta de GQL a {user{displayName profileImageURL}}.
Profile profileFromGql(const QJsonObject &response);
Profile fetchProfile(const QString &channel);
std::shared_ptr<Connector> create(const SourceInfo &source, Connector::PostFn post, Connector::ReportFn report);
} // namespace twitch

namespace kick {
QJsonArray splitContent(const QString &content);
QJsonObject toMessage(const QJsonObject &data, const QString &channel);
// Pusher events MessageDeletedEvent, UserBannedEvent and ChatroomClearEvent as a moderation event
// (see message.hpp); empty for any other event.
QJsonObject toDeletion(const QString &event, const QJsonObject &data);
LiveResult checkLive(const QString &channel);
// En la respuesta de /api/v2/channels/<canal>.
Profile profileFromChannel(const QJsonObject &channel);
Profile fetchProfile(const QString &channel);
std::shared_ptr<Connector> create(const SourceInfo &source, Connector::PostFn post, Connector::ReportFn report);
} // namespace kick

namespace youtube {
QJsonObject toMessage(const QJsonObject &action, const QString &channel);
// markChatItemAsDeletedAction, markChatItemsByAuthorAsDeletedAction, removeChatItemAction and
// removeChatItemByAuthorAction as a moderation event (see message.hpp); empty for other actions.
QJsonObject toDeletion(const QJsonObject &action);
// Siguiente continuación del chat y cuánto esperar (ms).
QString nextContinuation(const QJsonObject &liveChatContinuation, int &timeoutMs);
// Busca el id del vídeo en una URL (watch?v=, youtu.be/, /live/).
QString parseVideoId(const QString &value);
// En el HTML de /@canal/live: id del directo si está en vivo.
QString liveVideoFromPage(const QString &html);
// En el HTML de un directo: espectadores ahora mismo, o -1.
int viewersFromPage(const QString &html);
LiveResult checkLive(const SourceInfo &source);
// En el HTML de la página del canal o de un vídeo.
Profile profileFromPage(const QString &html);
Profile fetchProfile(const SourceInfo &source);
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
// En la misma respuesta: nombre y foto del usuario.
Profile profileFromRoom(const QJsonObject &response);
Profile fetchProfile(const QString &channel);
LiveResult checkLive(const QString &channel);
std::shared_ptr<Connector> create(const SourceInfo &source, Connector::PostFn post, Connector::ReportFn report);
} // namespace tiktok

namespace starvios {
// Una trama de Supabase Realtime. Por el mismo socket llegan tramas de control en texto JSON
// ([join_ref, ref, topic, event, payload]) y difusiones binarias con los mensajes del chat.
struct Frame {
	enum class Type {
		Invalid,   // datos mal formados
		Ignored,   // bien formada, pero de un tipo que aquí no interesa
		Control,   // texto: event es phx_reply, phx_error, phx_close...
		Broadcast, // binaria: event es el de la difusión ("msg"), payload su contenido JSON
	};
	Type type = Type::Invalid;
	QString ref;
	QString topic;
	QString event;
	QJsonObject payload;
};
// Decide el formato mirando el contenido: los backends de net:: no distinguen texto de binario.
Frame decodeFrame(QByteArrayView data);

// Emotes de la plataforma: código en minúsculas → URL de la imagen.
using EmoteMap = QHash<QString, QString>;
// En la respuesta de /api/public/emotes. Descarta las entradas con un código o una ruta sospechosos.
EmoteMap parseEmoteCatalog(const QJsonObject &response);
// Separa el texto de los :código: conocidos, como hace la página. Los enlaces quedan intactos.
QJsonArray splitBody(const QString &body, const EmoteMap &emotes);

// Nombre visible de una fila de live_chat_messages: display_name o username; vacío si no trae perfil.
QString authorOf(const QJsonObject &row);
// Convierte una fila en un mensaje. Devuelve un objeto vacío si no hay nada que mostrar (sin id,
// borrada, vacía). ownerId es el id del canal: su dueño lleva insignia aunque la fila no la traiga.
QJsonObject toMessage(const QJsonObject &row, const QString &channel, const QString &ownerId = {},
		      const EmoteMap &emotes = {}, bool recovered = false);

// A broadcast payload {op, msg} that deletes an already shown row (op DELETE, or UPDATE with
// is_deleted) as a moderation event (see message.hpp); empty otherwise.
QJsonObject toDeletion(const QJsonObject &payload);

// En la respuesta de /api/public/discovery. Rellena los ids solo si el directo está en vivo.
LiveResult parseDiscovery(const QJsonObject &response, QString &streamId, QString &channelId);
Profile profileFromDiscovery(const QJsonObject &response);
// En la respuesta de live_viewer_counts: espectadores de ese directo, o -1.
int viewersFromCounts(const QJsonArray &rows, const QString &streamId);

// Para recuperarse si Starvios cambia su clave pública: la ruta de /assets/client-*.js en el HTML
// de la portada, y la clave y la URL del proyecto dentro de ese módulo.
QString clientScriptFromHtml(const QString &html);
bool credentialsFromScript(const QString &script, QString &key, QString &projectUrl);

LiveResult checkLive(const QString &channel);
Profile fetchProfile(const QString &channel);

// Tiempos de la sesión; las pruebas los acortan.
struct Timing {
	int joinTimeoutMs = 15000;
	int heartbeatMs = 25000;
	int discoveryMs = 60000; // comprueba si el directo sigue siendo el mismo
	int offlineMs = 30000;   // sin directo: cada cuánto se vuelve a preguntar
};
std::shared_ptr<Connector> create(const SourceInfo &source, Connector::PostFn post, Connector::ReportFn report,
				  Timing timing = {});
// Olvida la clave recuperada y los emotes. Solo para las pruebas: ese estado es de todo el proceso.
void resetSharedState();
} // namespace starvios

namespace demo {
std::shared_ptr<Connector> create(const SourceInfo &source, Connector::PostFn post, Connector::ReportFn report);
} // namespace demo

} // namespace chat
