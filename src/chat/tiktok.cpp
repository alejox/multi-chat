// TikTok LIVE: WebSocket de la web de TikTok (protobuf). TikTok exige firmar la conexión;
// la firma la da Euler Stream (servicio gratuito, sin cuenta, con límite de conexiones por día).

#include "platforms.hpp"
#include "message.hpp"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace chat {
namespace tiktok {

// ---- protobuf ----

bool Proto::readVarint(quint64 &out)
{
	out = 0;
	for (int shift = 0; shift < 64; shift += 7) {
		if (pos_ >= data_.size())
			return false;
		const quint8 b = quint8(data_[pos_++]);
		out |= quint64(b & 0x7f) << shift;
		if (!(b & 0x80))
			return true;
	}
	return false;
}

bool Proto::next()
{
	quint64 key = 0;
	if (pos_ >= data_.size() || !readVarint(key))
		return false;
	field_ = int(key >> 3);
	varint_ = 0;
	bytes_ = {};
	switch (key & 7) {
	case 0: // varint
		return readVarint(varint_);
	case 1: // 64 bits
		if (data_.size() - pos_ < 8)
			return false;
		pos_ += 8;
		return true;
	case 2: { // bytes, string o mensaje
		quint64 len = 0;
		if (!readVarint(len) || len > quint64(data_.size() - pos_))
			return false;
		bytes_ = data_.sliced(pos_, qsizetype(len));
		pos_ += qsizetype(len);
		return true;
	}
	case 5: // 32 bits
		if (data_.size() - pos_ < 4)
			return false;
		pos_ += 4;
		return true;
	default:
		return false;
	}
}

void ProtoWriter::raw(quint64 value)
{
	while (value >= 0x80) {
		out_.append(char((value & 0x7f) | 0x80));
		value >>= 7;
	}
	out_.append(char(value));
}

ProtoWriter &ProtoWriter::varint(int field, quint64 value)
{
	raw(quint64(field) << 3);
	raw(value);
	return *this;
}

ProtoWriter &ProtoWriter::bytes(int field, QByteArrayView value)
{
	raw((quint64(field) << 3) | 2);
	raw(quint64(value.size()));
	out_.append(value.data(), value.size());
	return *this;
}

// ---- parseo ----

FetchResult parseFetchResult(QByteArrayView data)
{
	FetchResult r;
	Proto p(data);
	while (p.next()) {
		switch (p.field()) {
		case 1: { // BaseProtoMessage
			Event e;
			Proto m(p.bytes());
			while (m.next()) {
				if (m.field() == 1)
					e.method = m.string();
				else if (m.field() == 2)
					e.payload = m.bytes().toByteArray();
			}
			r.events << e;
			break;
		}
		case 5:
			r.internalExt = p.string();
			break;
		case 7: { // map<string, string>
			QString key, value;
			Proto m(p.bytes());
			while (m.next()) {
				if (m.field() == 1)
					key = m.string();
				else if (m.field() == 2)
					value = m.string();
			}
			r.routeParams.append({key, value});
			break;
		}
		case 9:
			r.needAck = p.varint() != 0;
			break;
		case 10:
			r.pushServer = p.string();
			break;
		}
	}
	return r;
}

namespace {

struct User {
	QString nickname, handle;
};

User parseUser(QByteArrayView data)
{
	User u;
	Proto p(data);
	while (p.next()) {
		if (p.field() == 3)
			u.nickname = p.string();
		else if (p.field() == 38)
			u.handle = p.string();
	}
	return u;
}

QString authorName(const User &u)
{
	return u.nickname.isEmpty() ? (u.handle.isEmpty() ? QStringLiteral("?") : u.handle) : u.nickname;
}

// UserIdentity: 2 suscriptor, 5 moderador, 6 el propio streamer.
QStringList identityBadges(QByteArrayView data)
{
	QStringList badges;
	Proto p(data);
	while (p.next()) {
		if (!p.varint())
			continue;
		if (p.field() == 6)
			badges.prepend("owner");
		else if (p.field() == 5)
			badges << "moderator";
		else if (p.field() == 2)
			badges << "subscriber";
	}
	return badges;
}

QJsonObject chatMessage(QByteArrayView data, const QString &channel)
{
	User user;
	QString content;
	MessageOptions o;
	Proto p(data);
	while (p.next()) {
		if (p.field() == 2)
			user = parseUser(p.bytes());
		else if (p.field() == 3)
			content = p.string();
		else if (p.field() == 18)
			o.badges = identityBadges(p.bytes());
	}
	if (content.isEmpty())
		return {};
	return makeMessage("tiktok", channel, authorName(user), QJsonArray{textPart(content)}, o);
}

QJsonObject giftMessage(QByteArrayView data, const QString &channel)
{
	User user;
	QString name;
	int type = 0, diamonds = 0;
	quint64 repeat = 1, repeatEnd = 0;
	Proto p(data);
	while (p.next()) {
		switch (p.field()) {
		case 5:
			repeat = p.varint();
			break;
		case 7:
			user = parseUser(p.bytes());
			break;
		case 9:
			repeatEnd = p.varint();
			break;
		case 15: {
			Proto g(p.bytes());
			while (g.next()) {
				if (g.field() == 16)
					name = g.string();
				else if (g.field() == 11)
					type = int(g.varint());
				else if (g.field() == 12)
					diamonds = int(g.varint());
			}
			break;
		}
		}
	}
	// Los regalos en racha (tipo 1) llegan uno por cada toque: solo mostramos el final.
	if (type == 1 && !repeatEnd)
		return {};
	MessageOptions o;
	o.kind = "highlight";
	if (diamonds > 0)
		o.amount = QStringLiteral("%1 💎").arg(diamonds * qMax<quint64>(repeat, 1));
	o.system = QStringLiteral("%1 envió %2%3")
			   .arg(authorName(user), repeat > 1 ? QStringLiteral("%1× ").arg(repeat) : QString(),
				name.isEmpty() ? QStringLiteral("un regalo") : name);
	return makeMessage("tiktok", channel, authorName(user), QJsonArray(), o);
}

} // namespace

QJsonObject toMessage(const Event &event, const QString &channel)
{
	if (event.method == "WebcastChatMessage")
		return chatMessage(event.payload, channel);
	if (event.method == "WebcastGiftMessage")
		return giftMessage(event.payload, channel);
	return {};
}

LiveResult parseRoom(const QJsonObject &response, QString &roomId)
{
	LiveResult r;
	const QJsonObject data = response.value("data").toObject();
	roomId = data.value("user").toObject().value("roomId").toString();
	const QJsonObject room = data.value("liveRoom").toObject();
	if (room.isEmpty())
		return r;
	// status: 2 en vivo, 4 terminado.
	r.live = room.value("status").toInt() == 2 ? 1 : 0;
	if (r.live == 1)
		r.viewers = room.value("liveRoomStats").toObject().value("userCount").toInt(-1);
	return r;
}

Profile profileFromRoom(const QJsonObject &response)
{
	const QJsonObject user = response.value("data").toObject().value("user").toObject();
	QString avatar = user.value("avatarThumb").toString();
	if (avatar.isEmpty())
		avatar = user.value("avatarMedium").toString();
	return {user.value("nickname").toString(), avatar};
}

namespace {

const char *const kSignServer = "https://api.eulerstream.com";

QString cleanHandle(const QString &channel)
{
	return channel.startsWith('@') ? channel.mid(1) : channel;
}

LiveResult fetchRoom(const QString &channel, QString &roomId, QString &error, Profile *profile = nullptr)
{
	const QString url = "https://www.tiktok.com/api-live/user/room/?aid=1988&app_name=tiktok_web"
			    "&device_platform=web_pc&sourceType=54&uniqueId=" +
			    QString::fromLatin1(QUrl::toPercentEncoding(cleanHandle(channel)));
	net::Response res = net::get(url.toStdString(), {{"Accept", "application/json"},
							 {"Referer", "https://www.tiktok.com/"}});
	if (!res.ok()) {
		error = res.status ? QStringLiteral("HTTP %1").arg(res.status) : QString::fromStdString(res.error);
		return {};
	}
	const QJsonObject json = QJsonDocument::fromJson(QByteArray::fromStdString(res.body)).object();
	if (json.value("message").toString() == "user_not_found") {
		error = QStringLiteral("el usuario no existe o no puede hacer directos");
		return {};
	}
	if (!json.value("data").isObject()) {
		// TikTok responde con una página HTML cuando bloquea o limita las consultas.
		error = QStringLiteral("respuesta inesperada de TikTok");
		return {};
	}
	if (profile)
		*profile = profileFromRoom(json);
	return parseRoom(json, roomId);
}

// "a=1; Path=/, b=2; Domain=..." → "tt-target-idc=useast1a; a=1; b=2"
QString cookieHeader(const std::string &setCookie)
{
	QStringList out;
	if (setCookie.find("tt-target-idc=") == std::string::npos)
		out << "tt-target-idc=useast1a";
	for (const QString &part : QString::fromStdString(setCookie).split(QRegularExpression("[;,]\\s*"))) {
		const qsizetype eq = part.indexOf('=');
		if (eq <= 0)
			continue;
		static const QStringList attributes = {"path", "domain", "expires", "max-age", "samesite"};
		if (attributes.contains(part.left(eq).trimmed().toLower()))
			continue;
		out << part.trimmed();
	}
	return out.join("; ");
}

QByteArray pushFrame(const char *type, QByteArrayView payload, quint64 logId = 0)
{
	ProtoWriter w;
	if (logId)
		w.varint(2, logId);
	w.bytes(6, "pb").bytes(7, type).bytes(8, payload);
	return w.data();
}

} // namespace

LiveResult checkLive(const QString &channel)
{
	QString roomId, error;
	return fetchRoom(channel, roomId, error);
}

Profile fetchProfile(const QString &channel)
{
	QString roomId, error;
	Profile profile;
	fetchRoom(channel, roomId, error, &profile);
	return profile;
}

class TikTokConnector : public Connector {
public:
	using Connector::Connector;

protected:
	void session() override
	{
		QString roomId, error;
		for (;;) {
			LiveResult room = fetchRoom(source_.channel, roomId, error);
			if (room.live == 1 && !roomId.isEmpty())
				break;
			if (room.live == -1 && !error.isEmpty())
				throw ChatError("no se pudo consultar TikTok (" + error + ")");
			report_("waiting", "esperando a que empiece el directo");
			if (wait(30000))
				return;
		}

		// 1. Euler Stream firma la conexión y da el servidor, los parámetros y las cookies.
		const QString signUrl = QStringLiteral("%1/webcast/rooms/%2/connect?client=obs-multichat&client_enter=true"
						       "&platform=web&user_agent=%3")
						.arg(QLatin1String(kSignServer), roomId,
						     QString::fromLatin1(QUrl::toPercentEncoding(net::kUserAgent)));
		net::Response res = net::get(signUrl.toStdString(), {{"Accept", "application/protobuf"}}, 20000);
		if (res.status == 429) {
			// Límite diario del servicio gratuito: no tiene sentido reintentar enseguida.
			const QString msg = "Euler Stream (firma de TikTok): demasiadas conexiones, se reintentará en 10 min";
			report_("error", msg);
			if (wait(600000))
				return;
			throw ChatError(msg);
		}
		if (!res.ok() || res.body.empty())
			throw ChatError(QStringLiteral("Euler Stream (firma de TikTok) respondió %1")
						.arg(res.status ? QString::number(res.status)
								: QString::fromStdString(res.error)));
		const FetchResult first = parseFetchResult(QByteArrayView(res.body.data(), qsizetype(res.body.size())));
		if (first.pushServer.isEmpty())
			throw ChatError("Euler Stream no devolvió el servidor de TikTok");

		// 2. URL del WebSocket: parámetros firmados + los de la web de TikTok.
		QStringList query;
		for (const auto &[key, value] : first.routeParams)
			if (!value.isEmpty())
				query << key + '=' + QString::fromLatin1(QUrl::toPercentEncoding(value));
		query << "aid=1988" << "app_name=tiktok_web" << "browser_platform=Win32" << "browser_name=Mozilla"
		      << "browser_online=true" << "cookie_enabled=true" << "device_platform=web" << "identity=audience"
		      << "live_id=12" << "sup_ws_ds_opt=1" << "update_version_code=2.0.0" << "version_code=180800"
		      << "client_enter=1" << "ws_direct=1" << "did_rule=3" << "heartbeat_duration=10000"
		      << "resp_content_type=protobuf" << "history_comment_count=0" << "compress=" << "room_id=" + roomId
		      << "version_code=270000";
		const QString wsUrl = first.pushServer + '?' + query.join('&');

		auto ws = openSocket(wsUrl.toStdString(), {{"Cookie", cookieHeader(res.header("X-Set-TT-Cookie")).toStdString()},
							   {"Origin", "https://www.tiktok.com"},
							   {"Sec-WebSocket-Protocol", "echo-protocol"}});

		// 3. Entrar en la sala y mantener la conexión con un latido cada 10 s.
		const quint64 room = roomId.toULongLong();
		ws->sendBinary(pushFrame("im_enter_room", ProtoWriter()
								  .varint(1, room)
								  .varint(4, 12)
								  .bytes(5, "audience")
								  .bytes(9, "0")
								  .data())
				       .toStdString());
		Heartbeat heartbeat(this, ws, room);

		bool connected = false;
		std::string raw;
		while (!stopped()) {
			if (ws->receive(raw) == net::WebSocket::Result::Closed) {
				if (stopped())
					return;
				throw ChatError(connected ? "TikTok cerró la conexión"
							  : "TikTok rechazó la conexión (puede que haya cambiado algo)");
			}
			if (!connected) {
				connected = true;
				report_("connected", QString());
			}
			QString type, compress;
			quint64 logId = 0;
			QByteArrayView payload;
			Proto frame(QByteArrayView(raw.data(), qsizetype(raw.size())));
			while (frame.next()) {
				if (frame.field() == 2) {
					logId = frame.varint();
				} else if (frame.field() == 5) {
					QString key, value;
					Proto h(frame.bytes());
					while (h.next())
						(h.field() == 1 ? key : value) = h.string();
					if (key == "compress_type")
						compress = value;
				} else if (frame.field() == 7) {
					type = frame.string();
				} else if (frame.field() == 8) {
					payload = frame.bytes();
				}
			}
			if (type != "msg")
				continue;
			if (!compress.isEmpty() && compress != "none")
				throw ChatError("TikTok envió los mensajes comprimidos (" + compress + ")");
			const FetchResult r = parseFetchResult(payload);
			if (r.needAck)
				ws->sendBinary(pushFrame("ack", r.internalExt.isEmpty() ? QByteArray("-")
											: r.internalExt.toUtf8(),
							 logId)
						       .toStdString());
			for (const Event &e : r.events) {
				if (e.method == "WebcastControlMessage" && streamEnded(e.payload))
					throw ChatError("el directo terminó");
				QJsonObject m = toMessage(e, source_.channel);
				if (!m.isEmpty())
					post_(m);
			}
		}
	}

private:
	// action 3 = directo terminado, 4 = suspendido.
	static bool streamEnded(QByteArrayView data)
	{
		Proto p(data);
		while (p.next())
			if (p.field() == 2)
				return p.varint() == 3 || p.varint() == 4;
		return false;
	}

	// Envía el latido en un hilo aparte mientras el conector espera mensajes.
	class Heartbeat {
	public:
		Heartbeat(TikTokConnector *owner, std::shared_ptr<net::WebSocket> ws, quint64 room)
			: thread_([this, owner, ws, room] {
				  quint64 seq = 1;
				  std::unique_lock<std::mutex> lock(mutex_);
				  while (!done_ && !owner->stopped()) {
					  ws->sendBinary(pushFrame("hb", ProtoWriter().varint(1, room).varint(2, seq++).data())
								 .toStdString());
					  cv_.wait_for(lock, std::chrono::seconds(10), [this] { return done_; });
				  }
			  })
		{
		}
		~Heartbeat()
		{
			{
				std::lock_guard<std::mutex> lock(mutex_);
				done_ = true;
			}
			cv_.notify_all();
			thread_.join();
		}

	private:
		std::mutex mutex_;
		std::condition_variable cv_;
		bool done_ = false;
		std::thread thread_; // el último: arranca cuando lo demás ya existe
	};
};

std::shared_ptr<Connector> create(const SourceInfo &s, Connector::PostFn post, Connector::ReportFn report)
{
	return std::make_shared<TikTokConnector>(s, std::move(post), std::move(report));
}

} // namespace tiktok
} // namespace chat
