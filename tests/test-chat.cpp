// Pruebas de los parsers (sin red ni OBS). Se compilan en CI y con tests/CMakeLists.txt.

#include "../src/chat/message.hpp"
#include "../src/chat/platforms.hpp"
#include "../src/chat/updates.hpp"

#include <QJsonDocument>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

using namespace chat;

namespace netstub {
using RequestHandler =
	std::function<net::Response(const std::string &method, const std::string &url, const std::string &body)>;
using SocketFactory = std::function<std::shared_ptr<net::WebSocket>(const std::string &url)>;
void set(RequestHandler handler, SocketFactory factory);
} // namespace netstub

// Datos de prueba en fixtures.cpp (moc no entiende los raw strings).
const char *fixture(int index);

namespace {

// WebSocket programado: guarda lo que envía el conector y le entrega las tramas de la prueba.
class FakeSocket : public net::WebSocket {
public:
	explicit FakeSocket(std::string socketUrl) : url(std::move(socketUrl)) {}

	Result receive(std::string &message) override
	{
		std::unique_lock<std::mutex> lock(mutex_);
		cv_.wait(lock, [this] { return closed_ || !incoming_.empty(); });
		if (incoming_.empty())
			return Result::Closed;
		message = std::move(incoming_.front());
		incoming_.pop_front();
		return Result::Message;
	}
	bool send(const std::string &text) override
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (closed_ || failSends_)
			return false;
		sent_.push_back(QString::fromStdString(text));
		return true;
	}
	bool sendBinary(const std::string &) override { return true; }
	void close() override
	{
		std::lock_guard<std::mutex> lock(mutex_);
		closed_ = true;
		cv_.notify_all();
	}

	void push(const QByteArray &frame)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		incoming_.push_back(frame.toStdString());
		cv_.notify_all();
	}
	QStringList sent()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return sent_;
	}
	// El envío falla pero receive sigue bloqueado, como en una conexión muerta sin aviso.
	void failSends()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		failSends_ = true;
	}
	bool closed()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return closed_;
	}

	const std::string url;

private:
	std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<std::string> incoming_;
	QStringList sent_;
	bool closed_ = false;
	bool failSends_ = false;
};

// Entorno de una prueba del conector: respuestas de red, sockets abiertos y lo que publica.
struct StarviosRig {
	std::mutex mutex;
	std::vector<std::shared_ptr<FakeSocket>> sockets;
	QList<QJsonObject> posted;
	QStringList statuses;
	QStringList requests;
	QString streamId = "s1";
	QString status = "live";
	std::function<net::Response(const std::string &, const std::string &)> extra;
	std::shared_ptr<Connector> connector;

	StarviosRig()
	{
		starvios::resetSharedState();
		netstub::set(
			[this](const std::string &method, const std::string &url, const std::string &body) {
				return handle(method, url, body);
			},
			[this](const std::string &url) {
				auto socket = std::make_shared<FakeSocket>(url);
				std::lock_guard<std::mutex> lock(mutex);
				sockets.push_back(socket);
				return socket;
			});
	}
	~StarviosRig()
	{
		if (connector) {
			connector->stop();
			connector->join();
		}
		netstub::set(nullptr, nullptr);
		starvios::resetSharedState();
	}

	net::Response handle(const std::string &method, const std::string &url, const std::string &body)
	{
		net::Response res;
		{
			std::lock_guard<std::mutex> lock(mutex);
			requests << QString::fromStdString(method + " " + url);
		}
		if (extra) {
			res = extra(url, body);
			if (res.status)
				return res;
		}
		if (url.find("/api/public/discovery") != std::string::npos) {
			std::lock_guard<std::mutex> lock(mutex);
			res.status = 200;
			res.body = QJsonDocument(QJsonObject{
							 {"channel", QJsonObject{{"id", "c1"}, {"username", "ana_test"}}},
							 {"stream", QJsonObject{{"id", streamId}, {"status", status}}},
						 }).toJson().toStdString();
			return res;
		}
		res.status = 404;
		return res;
	}

	void start(starvios::Timing timing = fastTiming())
	{
		SourceInfo info;
		QString error;
		parseEntry("starvios:ana_test", info, error);
		connector = starvios::create(
			info,
			[this](QJsonObject m) {
				std::lock_guard<std::mutex> lock(mutex);
				posted << m;
			},
			[this](const QString &status, const QString &detail) {
				std::lock_guard<std::mutex> lock(mutex);
				statuses << status + ':' + detail;
			},
			timing);
		connector->start();
	}
	static starvios::Timing fastTiming()
	{
		starvios::Timing t;
		t.joinTimeoutMs = 5000;
		t.heartbeatMs = 100000;
		t.discoveryMs = 100000;
		t.offlineMs = 50;
		return t;
	}

	static bool until(const std::function<bool()> &condition, int ms = 8000)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
		while (!condition()) {
			if (std::chrono::steady_clock::now() > deadline)
				return false;
			QThread::msleep(5);
		}
		return true;
	}
	std::shared_ptr<FakeSocket> socket(int index)
	{
		std::lock_guard<std::mutex> lock(mutex);
		return index < int(sockets.size()) ? sockets[size_t(index)] : nullptr;
	}
	std::shared_ptr<FakeSocket> waitSocket(int index)
	{
		std::shared_ptr<FakeSocket> out;
		until([&] { return (out = socket(index)) != nullptr; });
		return out;
	}
	int postedCount()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return int(posted.size());
	}
	QJsonObject postedAt(int index)
	{
		std::lock_guard<std::mutex> lock(mutex);
		return posted.value(index);
	}
	bool hasStatus(const QString &prefix)
	{
		std::lock_guard<std::mutex> lock(mutex);
		for (const QString &s : statuses)
			if (s.startsWith(prefix))
				return true;
		return false;
	}
	int requestCount(const QString &part)
	{
		std::lock_guard<std::mutex> lock(mutex);
		int n = 0;
		for (const QString &r : requests)
			n += r.contains(part) ? 1 : 0;
		return n;
	}
	QString lastRequest(const QString &part)
	{
		std::lock_guard<std::mutex> lock(mutex);
		for (auto it = requests.rbegin(); it != requests.rend(); ++it)
			if (it->contains(part))
				return *it;
		return {};
	}
	// Abre la sesión: espera el socket y el phx_join, y acepta la entrada al canal.
	std::shared_ptr<FakeSocket> joined(int index = 0)
	{
		auto s = waitSocket(index);
		if (!s)
			return nullptr;
		until([&] { return !s->sent().isEmpty(); });
		const QJsonArray join = QJsonDocument::fromJson(s->sent().value(0).toUtf8()).array();
		s->push(QJsonDocument(QJsonArray{join[0], join[1], join[2], "phx_reply",
						 QJsonObject{{"status", "ok"}, {"response", QJsonObject{}}}})
				.toJson(QJsonDocument::Compact));
		return s;
	}
};

QByteArray starviosMsg(const QJsonObject &row, const QByteArray &op = "INSERT")
{
	const QByteArray payload =
		QJsonDocument(QJsonObject{{"op", QString::fromLatin1(op)}, {"msg", row}}).toJson(QJsonDocument::Compact);
	const QByteArray topic = "realtime:stream:s1";
	QByteArray out;
	out.append(char(4));
	out.append(char(topic.size()));
	out.append(char(3));
	out.append(char(0));
	out.append(char(1));
	return out + topic + "msg" + payload;
}

// Difusión en texto construida según decode() del cliente de Realtime de la página (no es una captura real).
QByteArray textBroadcast(const QString &event, const QJsonObject &payload, const QString &topic = "realtime:stream:s1")
{
	const QJsonArray frame{QJsonValue::Null, QJsonValue::Null, topic, "broadcast",
			       QJsonObject{{"type", "broadcast"}, {"event", event}, {"payload", payload}}};
	return QJsonDocument(frame).toJson(QJsonDocument::Compact);
}

// Recoge lo que se escribe en el registro mientras existe.
class LogCapture {
public:
	LogCapture()
	{
		std::lock_guard<std::mutex> lock(mutex());
		lines().clear();
		previous_ = qInstallMessageHandler(&LogCapture::handler);
	}
	~LogCapture() { qInstallMessageHandler(previous_); }
	bool contains(const QString &part) const
	{
		std::lock_guard<std::mutex> lock(mutex());
		for (const QString &line : lines())
			if (line.contains(part))
				return true;
		return false;
	}

private:
	static void handler(QtMsgType, const QMessageLogContext &, const QString &text)
	{
		std::lock_guard<std::mutex> lock(mutex());
		lines() << text;
	}
	static std::mutex &mutex()
	{
		static std::mutex m;
		return m;
	}
	static QStringList &lines()
	{
		static QStringList l;
		return l;
	}
	QtMessageHandler previous_ = nullptr;
};

QJsonObject chatRow(const QString &id, const QString &body, const QString &created = "2026-01-02T03:04:05.100000+00:00",
		    bool profile = true)
{
	QJsonObject row{{"id", id}, {"user_id", "u1"}, {"body", body}, {"coins", 0}, {"kind", "chat"}, {"created_at", created}};
	if (profile)
		row.insert("profiles", QJsonObject{{"username", "ana_test"}, {"display_name", "Ana"}});
	return row;
}

QString firstText(const QJsonObject &message)
{
	return message["parts"].toArray()[0].toObject()["text"].toString();
}

} // namespace

class ChatTest : public QObject {
	Q_OBJECT

private:
	static SourceInfo entry(const QString &text)
	{
		SourceInfo s;
		QString error;
		if (!parseEntry(text, s, error))
			qWarning() << "parseEntry falló:" << text << error;
		return s;
	}

	// Trama binaria de Supabase Realtime construida según _decodeUserBroadcast del bundle de la
	// página (no es una captura real): tipo, longitudes de topic, evento y metadatos, codificación y
	// después esos campos seguidos del contenido.
	static QByteArray realtimeFrame(const QByteArray &topic, const QByteArray &event, const QByteArray &meta,
					const QByteArray &payload, char kind = 4, char encoding = 1)
	{
		QByteArray out;
		out.append(kind);
		out.append(char(topic.size()));
		out.append(char(event.size()));
		out.append(char(meta.size()));
		out.append(encoding);
		return out + topic + event + meta + payload;
	}

private slots:
	void starviosBroadcastFrame()
	{
		using starvios::Frame;
		const QByteArray payload = "{\"op\":\"INSERT\",\"msg\":{\"id\":\"m1\",\"body\":\"hóla\"}}";
		for (const QByteArray &meta : {QByteArray(), QByteArray("{\"id\":\"x\"}")}) {
			const Frame f = starvios::decodeFrame(realtimeFrame("realtime:stream:s1", "msg", meta, payload));
			QVERIFY(f.type == Frame::Type::Broadcast);
			QCOMPARE(f.topic, QString("realtime:stream:s1"));
			QCOMPARE(f.event, QString("msg"));
			QCOMPARE(f.payload["op"].toString(), QString("INSERT"));
			QCOMPARE(f.payload["msg"].toObject()["body"].toString(), QString("hóla"));
		}
		// Metadatos que no son JSON: la página los descartaría, aquí no invalidan el mensaje.
		QVERIFY(starvios::decodeFrame(realtimeFrame("t", "msg", "{", payload)).type == Frame::Type::Broadcast);
		// Bien formadas, pero sin contenido JSON o de otro tipo: se ignoran sin ser un error.
		QVERIFY(starvios::decodeFrame(realtimeFrame("t", "msg", "", "\x01\x02", 4, 0)).type == Frame::Type::Ignored);
		QVERIFY(starvios::decodeFrame(realtimeFrame("t", "msg", "", payload, 3)).type == Frame::Type::Ignored);
		QVERIFY(starvios::decodeFrame(realtimeFrame("t", "msg", "", payload, 9)).type == Frame::Type::Ignored);
	}

	void starviosMalformedFrames()
	{
		using starvios::Frame;
		QVERIFY(starvios::decodeFrame(QByteArray()).type == Frame::Type::Invalid);
		QVERIFY(starvios::decodeFrame(QByteArray("\x04", 1)).type == Frame::Type::Invalid);
		const QByteArray good = realtimeFrame("topic", "msg", "{}", "{\"op\":\"INSERT\"}");
		// Cortada en cualquier punto, nunca se lee fuera de los límites ni se da por buena.
		for (qsizetype cut = 0; cut < good.size(); ++cut)
			QVERIFY(starvios::decodeFrame(QByteArrayView(good).first(cut)).type == Frame::Type::Invalid);
		QVERIFY(starvios::decodeFrame(realtimeFrame("t", "msg", "", "no json")).type == Frame::Type::Invalid);
		QVERIFY(starvios::decodeFrame(realtimeFrame("t", "msg", "", "[1,2]")).type == Frame::Type::Invalid);
		QVERIFY(starvios::decodeFrame(QByteArray(32, char(0xFF))).type != Frame::Type::Broadcast);
		for (const char *text : {"[", "{}", "[1,2,3]", "\"hola\"", "[null,null,\"t\",\"e\"]"})
			QVERIFY(starvios::decodeFrame(QByteArray(text)).type == Frame::Type::Invalid);
	}

	void starviosControlFrame()
	{
		using starvios::Frame;
		const Frame reply = starvios::decodeFrame(QByteArray(
			"[\"1\",\"1\",\"realtime:stream:s1\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]"));
		QVERIFY(reply.type == Frame::Type::Control);
		QCOMPARE(reply.ref, QString("1"));
		QCOMPARE(reply.topic, QString("realtime:stream:s1"));
		QCOMPARE(reply.event, QString("phx_reply"));
		QCOMPARE(reply.payload["status"].toString(), QString("ok"));
		const Frame beat = starvios::decodeFrame(QByteArray("[null,\"7\",\"phoenix\",\"phx_reply\",{}]"));
		QVERIFY(beat.type == Frame::Type::Control);
		QCOMPARE(beat.ref, QString("7"));
		// ref numérico o nulo, y un contenido que no es un objeto: no rompen la trama.
		const Frame other = starvios::decodeFrame(QByteArray("[null,5,\"t\",\"system\",null]"));
		QVERIFY(other.type == Frame::Type::Control);
		QCOMPARE(other.ref, QString("5"));
		QVERIFY(other.payload.isEmpty());
		// Una difusión en texto se entrega igual que la binaria: el evento y el contenido de dentro.
		const Frame text = starvios::decodeFrame(textBroadcast("msg", QJsonObject{{"op", "INSERT"}}));
		QVERIFY(text.type == Frame::Type::Broadcast);
		QCOMPARE(text.topic, QString("realtime:stream:s1"));
		QCOMPARE(text.event, QString("msg"));
		QCOMPARE(text.payload["op"].toString(), QString("INSERT"));
		// Sin evento o sin contenido no es una difusión válida: queda como control.
		for (const char *bad : {"[null,null,\"t\",\"broadcast\",{\"payload\":{}}]",
					"[null,null,\"t\",\"broadcast\",{\"event\":\"msg\",\"payload\":3}]"})
			QVERIFY(starvios::decodeFrame(QByteArray(bad)).type == Frame::Type::Control);
	}

	void starviosEntries()
	{
		QCOMPARE(entry("starvios:Ana_Test").key, QString("starvios:ana_test"));
		QCOMPARE(entry("starvios:@ana_test").channel, QString("ana_test"));
		QCOMPARE(entry("https://starvios.com/live/Ana_Test").key, QString("starvios:ana_test"));
		QCOMPARE(entry("https://www.starvios.com/live/ana_test/").channel, QString("ana_test"));
		QCOMPARE(entry("starvios.com/live/ana_test?ref=x#chat").channel, QString("ana_test"));
		QCOMPARE(entry("starvios.com/live/ana_test").key, QString("starvios:ana_test"));
		QCOMPARE(entry("www.starvios.com/live/ana_test").key, QString("starvios:ana_test"));
		QCOMPARE(entry("http://starvios.com/live/ana_test").key, QString("starvios:ana_test"));
		QCOMPARE(entry("https://starvios.com/live/ana_test/").key, QString("starvios:ana_test"));
		QCOMPARE(entry("https://starvios.com/live/ana_test/?ref=x").key, QString("starvios:ana_test"));
		QCOMPARE(entry("https://starvios.com/live/ana_test?ref=x").key, QString("starvios:ana_test"));
		QCOMPARE(entry("HTTPS://StarVios.com/live/Ana_Test").key, QString("starvios:ana_test"));
		QCOMPARE(entry("  https://starvios.com/live/ana_test  ").key, QString("starvios:ana_test"));
		// Usuario suelto con la plataforma elegida: la ventana de chats antepone "starvios:".
		QCOMPARE(entry("starvios:ana_test").key, QString("starvios:ana_test"));
		QCOMPARE(entry("starvios:@ana_test").key, QString("starvios:ana_test"));
		// La página del canal, y las demás rutas de la web que llevan al mismo canal.
		for (const QString &path : {"ana_test", "canal/ana_test", "directo/ana_test", "live/ana_test", "Canal/Ana_Test",
					    "DIRECTO/ana_test", "LIVE/ana_test", "Ana_Test"})
			for (const QString &prefix : {"https://starvios.com/", "http://starvios.com/", "https://www.starvios.com/",
						      "HTTPS://StarVios.com/", "starvios.com/", "www.starvios.com/"})
				for (const QString &suffix : {"", "/", "?ref=x", "/?ref=x", "#chat", "  "})
					QCOMPARE(entry(prefix + path + suffix).key, QString("starvios:ana_test"));
		QString error;
		SourceInfo s;
		for (const char *bad :
		     {"starvios:", "starvios:ab", "starvios:mal nombre", "starvios:a/b", "starvios:../etc", "starvios:a&b=c",
		      "starvios:nombre_de_mas_de_veinticinco_letras", "https://starvios.com/clip/abc", "https://starvios.com/",
		      "https://starvios.com/live/", "https://starvios.com/live/mal%20nombre", "https://starvios.com/live?x=1",
		      "https://starvios.com/live/?x=1", "www.starvios.com/", "https://evil.com/live/ana_test",
		      "https://starvios.com.evil.io/live/ana_test", "ana_test", "https://evil.com/ana_test",
		      "https://starvios.com.evil.io/ana_test", "https://starvios.com/canal", "https://starvios.com/canal/",
		      "https://starvios.com/directo", "https://starvios.com/LIVE", "starvios.com/live", "starvios.com/earn",
		      "https://starvios.com/auth", "https://starvios.com/terms/", "https://starvios.com/privacy?x=1",
		      "https://starvios.com/api", "https://starvios.com/assets", "https://starvios.com/Settings",
		      "https://starvios.com/explore", "https://starvios.com/clips", "https://starvios.com/studio",
		      "https://starvios.com/api/public/discovery", "https://starvios.com/ana_test/clips",
		      "https://starvios.com/live/ana_test/extra", "https://starvios.com/popout/chat/ana_test",
		      "https://starvios.com/clip/new/ana_test", "https://starvios.com/@ana_test", "https://starvios.com/live/@ana_test",
		      "https://starvios.com/ab", "https://starvios.com/mal%20nombre", "https://starvios.com/?u=ana_test"})
			QVERIFY2(!parseEntry(bad, s, error), bad);
		QCOMPARE(platformName("starvios"), QString("Starvios"));
	}

	void starviosChatMessage()
	{
		const QJsonObject row = QJsonDocument::fromJson(fixture(10)).object();
		const starvios::EmoteMap emotes{{"abc_d", "https://starvios.com/api/public/img?p=c1%2Fx.png"}};
		const QJsonObject m = starvios::toMessage(row, "ana_test", "c9", emotes);
		QCOMPARE(m["platform"].toString(), QString("starvios"));
		QCOMPARE(m["channel"].toString(), QString("ana_test"));
		QCOMPARE(m["id"].toString(), QString("m1"));
		QCOMPARE(m["author"].toString(), QString("Ana Test"));
		QCOMPARE(m["color"].toString(), QString("#12ab9F"));
		QCOMPARE(m["kind"].toString(), QString("chat"));
		QVERIFY(!m.contains("amount"));
		QVERIFY(!m.contains("recovered"));
		QStringList badges;
		for (const QJsonValue b : m["badges"].toArray())
			badges << b.toString();
		QCOMPARE(badges, QStringList({"vip", "moderator", "subscriber", "owner", "staff", "verified"}));
		// El :código: conocido es un emote; el del enlace y el desconocido siguen siendo texto.
		const QJsonArray parts = m["parts"].toArray();
		QCOMPARE(parts.size(), qsizetype(3));
		QCOMPARE(parts[0].toObject()["text"].toString(), QString("hola "));
		QCOMPARE(parts[1].toObject()["type"].toString(), QString("emote"));
		QCOMPARE(parts[1].toObject()["name"].toString(), QString(":abc_d:"));
		QCOMPARE(parts[1].toObject()["url"].toString(), emotes.value("abc_d"));
		QCOMPARE(parts[2].toObject()["text"].toString(), QString(" y https://x.io/:abc_d: fin :nada:"));
		QVERIFY(starvios::toMessage(row, "ana_test", {}, {}, true)["recovered"].toBool());
	}

	void starviosAuthorAndBadges()
	{
		QJsonObject row = chatRow("m2", "hola");
		QCOMPARE(starvios::authorOf(row), QString("Ana"));
		row.insert("profiles", QJsonObject{{"username", "ana_test"}, {"display_name", ""}});
		QCOMPARE(starvios::authorOf(row), QString("ana_test"));
		// Color que no es un color: no se copia al mensaje.
		row.insert("profiles", QJsonObject{{"username", "x_y"}, {"chat_color", "red;}"}});
		QVERIFY(!starvios::toMessage(row, "c").contains("color"));
		// Sin perfil: mensaje con una etiqueta neutra, nunca un error.
		row.remove("profiles");
		QVERIFY(starvios::authorOf(row).isEmpty());
		QVERIFY(!starvios::toMessage(row, "c")["author"].toString().isEmpty());
		// Banderas del perfil.
		row.insert("profiles", QJsonObject{{"username", "x_y"}, {"is_verified", true}, {"is_team", true}});
		QCOMPARE(starvios::toMessage(row, "c")["badges"].toArray().size(), qsizetype(2));
		// El dueño sin insignias en la fila, igual que en la página.
		row.insert("profiles", QJsonObject{{"username", "x_y"}});
		const QJsonObject owner = starvios::toMessage(row, "c", "u1");
		QCOMPARE(owner["badges"].toArray()[0].toString(), QString("owner"));
	}

	void starviosPaidAndSystemMessages()
	{
		QJsonObject row = chatRow("m3", "toma");
		row.insert("coins", 250);
		QJsonObject m = starvios::toMessage(row, "c");
		QCOMPARE(m["kind"].toString(), QString("highlight"));
		QCOMPARE(m["amount"].toString(), QString("250 Starvies"));
		QCOMPARE(firstText(m), QString("toma"));
		row.insert("coins", 1);
		QCOMPARE(starvios::toMessage(row, "c")["amount"].toString(), QString("1 Starvie"));
		// Con Starvies pero sin texto sigue siendo una donación que hay que mostrar.
		row.insert("body", "");
		QCOMPARE(starvios::toMessage(row, "c")["amount"].toString(), QString("1 Starvie"));

		const auto withKind = [](const QString &kind, const QJsonObject &meta, const QString &body = {}) {
			QJsonObject r = chatRow("k", body);
			r.insert("kind", kind);
			r.insert("meta", meta);
			return starvios::toMessage(r, "c");
		};
		// Un "%2" en el nombre es texto, no un marcador del formato.
		QJsonObject tricky = chatRow("k", "");
		tricky.insert("kind", "raid");
		tricky.insert("meta", QJsonObject{{"viewers", 2}});
		tricky.insert("profiles", QJsonObject{{"username", "a%2b%3"}});
		QVERIFY(starvios::toMessage(tricky, "c")["system"].toString().startsWith("a%2b%3 está"));

		QJsonObject raid = withKind("raid", {{"viewers", 12}});
		QCOMPARE(raid["kind"].toString(), QString("highlight"));
		QVERIFY(raid["system"].toString().contains("12"));
		QVERIFY(raid["system"].toString().contains("Ana"));
		QVERIFY(raid["parts"].toArray().isEmpty());

		const QJsonObject reward{{"reward_title", "Lluvia"}, {"cost", 70}, {"emoji", "X"}, {"has_input", false}};
		QJsonObject redeemed = withKind("reward", reward, "no se muestra");
		QVERIFY(redeemed["system"].toString().contains("Lluvia"));
		QCOMPARE(redeemed["amount"].toString(), QString("70"));
		QVERIFY(redeemed["parts"].toArray().isEmpty());
		QJsonObject withInput = reward;
		withInput.insert("has_input", true);
		QCOMPARE(firstText(withKind("reward", withInput, "léelo")), QString("léelo"));

		QVERIFY(withKind("sub", {{"months", 3}})["system"].toString().contains("3"));
		QJsonObject gift = withKind("gift_sub", {{"recipient", "beto"}, {"anonymous", true}});
		QVERIFY(gift["system"].toString().contains("beto"));
		QVERIFY(!gift["system"].toString().contains("Ana"));
		QCOMPARE(withKind("starfall", {}, "cae una estrella")["system"].toString(), QString("cae una estrella"));
		QVERIFY(withKind("pumpkin_win", {})["system"].toString().contains("Ana"));
		// Otro tipo cualquiera con texto se muestra como chat, igual que en la página.
		QCOMPARE(firstText(withKind("algo_nuevo", {}, "hola")), QString("hola"));
	}

	void starviosMessagesNotShown()
	{
		QVERIFY(starvios::toMessage(QJsonObject{}, "c").isEmpty());
		QVERIFY(starvios::toMessage(chatRow("", "sin id"), "c").isEmpty());
		QVERIFY(starvios::toMessage(chatRow("m", ""), "c").isEmpty());
		QJsonObject deleted = chatRow("m", "hola");
		deleted.insert("is_deleted", true);
		QVERIFY(starvios::toMessage(deleted, "c").isEmpty());
		QJsonObject numeric = chatRow("m", "hola");
		numeric.insert("id", 5);
		numeric.insert("body", 7);
		numeric.insert("meta", "x");
		numeric.insert("profiles", "x");
		numeric.insert("badges", "x");
		QVERIFY(starvios::toMessage(numeric, "c").isEmpty()); // tipos inesperados: sin romperse
	}

	void starviosDiscovery()
	{
		const QJsonObject live = QJsonDocument::fromJson(fixture(11)).object();
		QString stream, owner;
		LiveResult r = starvios::parseDiscovery(live, stream, owner);
		QCOMPARE(r.live, 1);
		QCOMPARE(stream, QString("s1"));
		QCOMPARE(owner, QString("c1"));
		const Profile p = starvios::profileFromDiscovery(live);
		QCOMPARE(p.name, QString("Ana Test"));
		QCOMPARE(p.avatar, QString("https://starvios.com/api/public/img?p=c1%2Ffoto.png&w=128&sq=1"));

		// La foto solo se pide a los sitios que usa la página: la petición saldría desde la IP del usuario.
		const auto avatarOf = [&live](const QString &value) {
			QJsonObject channel = live["channel"].toObject();
			channel.insert("avatar_url", value);
			QJsonObject response = live;
			response.insert("channel", channel);
			return starvios::profileFromDiscovery(response).avatar;
		};
		for (const char *good : {"https://starvios.com/api/public/avatar/ana_test",
					 "https://naiijcvngtkkocligwvk.supabase.co/storage/v1/object/public/a.png"})
			QCOMPARE(avatarOf(good), QString(good));
		for (const char *bad :
		     {"https://evil.example/a.png", "https://starvios.com.evil.example/a.png", "https://evil.example/starvios.com/a.png",
		      "https://starvios.com@evil.example/a.png", "https://starvios.com:8443/a.png", "http://starvios.com/a.png",
		      "https://evil.example\\@starvios.com/a.png", "https://starvios.com\\@evil.example/a.png",
		      "https://user:pass@starvios.com/a.png", "https://otro-proyecto.supabase.co/a.png", "https://www.starvios.com.", ""})
			QVERIFY(avatarOf(bad).isEmpty());

		QJsonObject ended = live;
		ended.insert("stream", QJsonObject{{"id", "s1"}, {"status", "ended"}});
		stream.clear();
		QCOMPARE(starvios::parseDiscovery(ended, stream, owner).live, 0);
		QVERIFY(stream.isEmpty());
		QJsonObject none = live;
		none.insert("stream", QJsonValue::Null);
		QCOMPARE(starvios::parseDiscovery(none, stream, owner).live, 0);
		QCOMPARE(starvios::parseDiscovery(QJsonObject{}, stream, owner).live, -1);
		// Un id que no se puede usar sin más en una URL o un topic no se da por bueno.
		QJsonObject weird = live;
		weird.insert("stream", QJsonObject{{"id", "a/b&c"}, {"status", "live"}});
		QCOMPARE(starvios::parseDiscovery(weird, stream, owner).live, -1);

		const QJsonArray counts{QJsonObject{{"stream_id", "s0"}, {"viewers", 3}},
					QJsonObject{{"stream_id", "s1"}, {"viewers", 42}}};
		QCOMPARE(starvios::viewersFromCounts(counts, "s1"), 42);
		QCOMPARE(starvios::viewersFromCounts(counts, "s2"), -1);
		QCOMPARE(starvios::viewersFromCounts(QJsonArray{"x"}, "s1"), -1);
	}

	void starviosEmoteCatalog()
	{
		const starvios::EmoteMap emotes = starvios::parseEmoteCatalog(QJsonDocument::fromJson(fixture(12)).object());
		QCOMPARE(emotes.size(), qsizetype(1));
		QCOMPARE(emotes.value("abc_d"), QString("https://starvios.com/api/public/img?p=c1%2Fabc%20d.png"));
		QVERIFY(starvios::parseEmoteCatalog(QJsonObject{{"rows", "x"}}).isEmpty());
		// Mayúsculas indiferentes, como en la página, y códigos pegados.
		const QJsonArray parts = starvios::splitBody(":ABC_D::abc_d:", emotes);
		QCOMPARE(parts.size(), qsizetype(2));
		QCOMPARE(parts[1].toObject()["type"].toString(), QString("emote"));
		QVERIFY(starvios::splitBody("", emotes).isEmpty());
		QCOMPARE(starvios::splitBody(":abc_d:", {})[0].toObject()["text"].toString(), QString(":abc_d:"));
	}

	void starviosCredentialRecovery()
	{
		QCOMPARE(starvios::clientScriptFromHtml(fixture(13)), QString("/assets/client-D8x_9-q.js"));
		QVERIFY(starvios::clientScriptFromHtml("<script src=\"https://evil.example/assets/client-a.js\">").isEmpty());
		QVERIFY(starvios::clientScriptFromHtml("<html></html>").isEmpty());
		QString key, url;
		QVERIFY(starvios::credentialsFromScript(fixture(14), key, url));
		QCOMPARE(key, QString("sb_publishable_Nueva-Clave_123"));
		QCOMPARE(url, QString("https://abcdefgh.supabase.co"));
		// Solo sin comillas invertidas también vale; otro dominio no.
		QVERIFY(starvios::credentialsFromScript("k=\"sb_publishable_abcdefgh\";u=\"https://zz1.supabase.co\"", key, url));
		QVERIFY(!starvios::credentialsFromScript("k=`sb_publishable_abcdefgh`,u=`https://evil.example`", key, url));
		QVERIFY(!starvios::credentialsFromScript("u=`https://zz1.supabase.co`", key, url));
	}

	void starviosSessionShowsOnlyNewLiveMessages()
	{
		StarviosRig rig;
		rig.start();
		auto ws = rig.waitSocket(0);
		QVERIFY(ws);
		QVERIFY(QString::fromStdString(ws->url).startsWith("wss://naiijcvngtkkocligwvk.supabase.co/realtime/v1/websocket?apikey=sb_publishable_"));
		QVERIFY(QString::fromStdString(ws->url).endsWith("&vsn=2.0.0"));
		ws = rig.joined();
		const QJsonArray join = QJsonDocument::fromJson(ws->sent().value(0).toUtf8()).array();
		QCOMPARE(join[2].toString(), QString("realtime:stream:s1"));
		QCOMPARE(join[3].toString(), QString("phx_join"));
		QJsonObject config = join[4].toObject()["config"].toObject();
		QCOMPARE(config["presence"].toObject()["enabled"].toBool(true), false);
		QCOMPARE(config["broadcast"].toObject()["self"].toBool(true), false);
		QVERIFY(StarviosRig::until([&] { return rig.hasStatus("connected"); }));

		// El mensaje normal, un duplicado, una edición, uno borrado y formas desconocidas.
		ws->push(starviosMsg(chatRow("m1", "uno")));
		ws->push(starviosMsg(chatRow("m1", "uno")));
		ws->push(starviosMsg(chatRow("m1", "uno editado"), "UPDATE"));
		ws->push(starviosMsg(chatRow("m9", "nunca visto"), "UPDATE"));
		QJsonObject deleted = chatRow("m2", "borrado");
		deleted.insert("is_deleted", true);
		ws->push(starviosMsg(deleted));
		ws->push(QByteArray("\x04\x01", 2));
		ws->push("nada que ver");
		ws->push(starviosMsg(QJsonObject{{"sin", "id"}}));
		ws->push("[null,null,\"realtime:stream:s1\",\"presence_state\",{}]");
		ws->push(starviosMsg(chatRow("m3", "dos")));
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() >= 2; }));
		QThread::msleep(50);
		QCOMPARE(rig.postedCount(), 2);
		QCOMPARE(firstText(rig.postedAt(0)), QString("uno"));
		QCOMPARE(firstText(rig.postedAt(1)), QString("dos"));
		QCOMPARE(rig.postedAt(0)["author"].toString(), QString("Ana"));
		// Nada del historial al conectar.
		QCOMPARE(rig.requestCount("live_chat_messages"), 0);
	}

	void starviosTextBroadcast()
	{
		StarviosRig rig;
		rig.start();
		auto ws = rig.joined();
		QVERIFY(ws);
		const QByteArray frame = textBroadcast("msg", QJsonObject{{"op", "INSERT"}, {"msg", chatRow("m1", "en texto")}});
		ws->push(frame);
		ws->push(frame);
		ws->push(starviosMsg(chatRow("m1", "copia binaria")));
		ws->push(textBroadcast("msg", QJsonObject{{"op", "UPDATE"}, {"msg", chatRow("m9", "editado")}}));
		ws->push(starviosMsg(chatRow("m2", "binario")));
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() >= 2; }));
		QThread::msleep(50);
		QCOMPARE(rig.postedCount(), 2);
		QCOMPARE(firstText(rig.postedAt(0)), QString("en texto"));
		QCOMPARE(firstText(rig.postedAt(1)), QString("binario"));
	}

	void starviosUnknownEventsAreLogged()
	{
		LogCapture log;
		const QByteArray payload = QJsonDocument(QJsonObject{{"secreto", "contenido"}}).toJson(QJsonDocument::Compact);
		{
			StarviosRig rig;
			rig.start();
			auto ws = rig.joined();
			QVERIFY(ws);
			// Los eventos que la propia página espera en este canal no son una forma desconocida.
			for (const char *event : {"system", "presence_state", "presence_diff", "postgres_changes", "access_token"})
				ws->push(QJsonDocument(QJsonArray{QJsonValue::Null, QJsonValue::Null, "realtime:stream:s1", event,
								  QJsonObject{}})
						 .toJson(QJsonDocument::Compact));
			for (const char *event : {"live_row", "ping", "raid", "collaboration"})
				ws->push(realtimeFrame("realtime:stream:s1", event, "", payload));
			// Otro canal tampoco es asunto de este conector.
			ws->push(textBroadcast("de_otro_canal", {}, "realtime:stream:otro"));
			ws->push(starviosMsg(chatRow("m1", "marca")));
			QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 1; }));
			QVERIFY(!log.contains("evento"));
			// Un evento nuevo deja rastro con su nombre, nunca con el contenido.
			ws->push(textBroadcast("evento_nuevo", QJsonObject{{"secreto", "contenido"}}));
			QVERIFY(StarviosRig::until([&] { return log.contains("evento_nuevo"); }));
			QVERIFY(!log.contains("secreto"));
		}
		// Cada forma, en una sesión propia: el registro admite una línea cada varios minutos.
		const QList<QPair<QByteArray, QString>> others{
			{realtimeFrame("realtime:stream:s1", "binario_nuevo", "", payload), "binario_nuevo"},
			{"[null,null,\"realtime:stream:s1\",\"control_nuevo\",{}]", "control_nuevo"},
			{"[null,null,\"realtime:stream:s1\",\"broadcast\",{\"event\":\"msg\"}]", "broadcast"},
		};
		for (const auto &[frame, expected] : others) {
			LogCapture each;
			StarviosRig rig;
			rig.start();
			auto ws = rig.joined();
			QVERIFY(ws);
			ws->push(frame);
			QVERIFY(StarviosRig::until([&, name = expected] { return each.contains(name); }));
		}
	}

	void starviosHeartbeats()
	{
		StarviosRig rig;
		starvios::Timing t = StarviosRig::fastTiming();
		t.heartbeatMs = 100;
		rig.start(t);
		auto ws = rig.joined();
		QVERIFY(ws);
		const auto beats = [&ws] {
			QStringList out;
			for (const QString &sent : ws->sent())
				if (sent.contains("\"heartbeat\""))
					out << sent;
			return out;
		};
		QVERIFY(StarviosRig::until([&] { return !beats().isEmpty(); }));
		const QJsonArray beat = QJsonDocument::fromJson(beats().value(0).toUtf8()).array();
		QVERIFY(beat[0].isNull());
		QCOMPARE(beat[2].toString(), QString("phoenix"));
		QCOMPARE(beat[3].toString(), QString("heartbeat"));

		// Mientras se responde a cada latido, el socket sigue abierto.
		const auto answer = [&ws](const QJsonValue &ref) {
			ws->push(QJsonDocument(QJsonArray{QJsonValue::Null, ref, "phoenix", "phx_reply", QJsonObject{{"status", "ok"}}})
					 .toJson(QJsonDocument::Compact));
		};
		qsizetype answered = 0;
		const bool healthy = StarviosRig::until(
			[&] {
				const QStringList sent = beats();
				for (; answered < sent.size(); ++answered)
					answer(QJsonDocument::fromJson(sent[answered].toUtf8()).array()[1]);
				return answered >= 6 || ws->closed();
			},
			4000);
		QVERIFY(healthy);
		QVERIFY(!ws->closed());
		QVERIFY(!rig.socket(1));

		// Una respuesta a otro latido no cuenta: sin la suya, cierra y vuelve a conectar.
		answer("no-es-este");
		QVERIFY(StarviosRig::until([&] { return ws->closed(); }, 2000));
		QVERIFY(rig.waitSocket(1));
	}

	void starviosFailedHeartbeatReconnects()
	{
		StarviosRig rig;
		starvios::Timing t = StarviosRig::fastTiming();
		t.heartbeatMs = 30;
		rig.start(t);
		auto ws = rig.joined();
		QVERIFY(ws);
		QVERIFY(StarviosRig::until([&] { return rig.hasStatus("connected"); }));
		ws->failSends();
		QVERIFY(StarviosRig::until([&] { return ws->closed(); }, 2000));
		QVERIFY(rig.waitSocket(1));
	}

	void starviosChannelClosedOrFailedReconnects()
	{
		for (const char *event : {"phx_close", "phx_error"}) {
			StarviosRig rig;
			rig.start();
			auto ws = rig.joined();
			QVERIFY(ws);
			QVERIFY(StarviosRig::until([&] { return rig.hasStatus("connected"); }));
			// El de otro canal no afecta a este.
			ws->push(QJsonDocument(QJsonArray{QJsonValue::Null, QJsonValue::Null, "realtime:stream:otro", event, QJsonObject{}})
					 .toJson(QJsonDocument::Compact));
			ws->push(starviosMsg(chatRow("m1", "sigue")));
			QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 1; }));
			QVERIFY(!rig.hasStatus("error"));
			ws->push(QJsonDocument(QJsonArray{QJsonValue::Null, QJsonValue::Null, "realtime:stream:s1", event, QJsonObject{}})
					 .toJson(QJsonDocument::Compact));
			QVERIFY(StarviosRig::until([&] { return rig.hasStatus("error:Starvios cerró el canal"); }));
			QVERIFY(StarviosRig::until([&] { return ws->closed(); }));
			QVERIFY(rig.waitSocket(1));
		}
	}

	void starviosJoinTimeoutReconnects()
	{
		StarviosRig rig;
		starvios::Timing t = StarviosRig::fastTiming();
		t.joinTimeoutMs = 30;
		rig.start(t);
		auto ws = rig.waitSocket(0);
		QVERIFY(ws);
		QVERIFY(StarviosRig::until([&] { return ws->closed(); }));
		QVERIFY(StarviosRig::until([&] { return rig.hasStatus("error:Starvios no aceptó la conexión"); }));
		QVERIFY(!rig.hasStatus("connected"));
		QVERIFY(rig.waitSocket(1));
	}

	void starviosStreamEndingWaitsWithoutError()
	{
		StarviosRig rig;
		starvios::Timing t = StarviosRig::fastTiming();
		t.discoveryMs = 30;
		rig.start(t);
		auto ws = rig.joined();
		QVERIFY(ws);
		QVERIFY(StarviosRig::until([&] { return rig.hasStatus("connected"); }));
		{
			std::lock_guard<std::mutex> lock(rig.mutex);
			rig.status = "ended";
		}
		QVERIFY(StarviosRig::until([&] { return ws->closed(); }));
		QVERIFY(StarviosRig::until([&] { return rig.hasStatus("waiting"); }));
		QVERIFY(!rig.hasStatus("error"));
		QVERIFY(!rig.socket(1));
	}

	void starviosMissingAuthorIsFetchedOnce()
	{
		StarviosRig rig;
		std::atomic<int> rowsOk{1};
		rig.extra = [&](const std::string &url, const std::string &) {
			net::Response res;
			if (url.find("/rest/v1/live_chat_messages") == std::string::npos)
				return res;
			if (!rowsOk) {
				res.status = 500;
				return res;
			}
			res.status = 200;
			res.body = QJsonDocument(QJsonArray{chatRow("m1", "uno")}).toJson().toStdString();
			return res;
		};
		rig.start();
		auto ws = rig.joined();
		QVERIFY(ws);
		ws->push(starviosMsg(chatRow("m1", "uno", {}, false)));
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 1; }));
		QCOMPARE(rig.postedAt(0)["author"].toString(), QString("Ana"));
		QCOMPARE(rig.requestCount("id=eq.m1"), 1);
		QVERIFY(rig.lastRequest("id=eq.m1").startsWith("GET "));
		// El siguiente mensaje de esa persona ya no necesita otra consulta.
		ws->push(starviosMsg(chatRow("m2", "dos", {}, false)));
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 2; }));
		QCOMPARE(rig.postedAt(1)["author"].toString(), QString("Ana"));
		QCOMPARE(rig.requestCount("id=eq.m2"), 0);
		// Otra persona, y la consulta falla: etiqueta neutra y el socket sigue vivo.
		rowsOk = 0;
		QJsonObject stranger = chatRow("m3", "tres", {}, false);
		stranger.insert("user_id", "u2");
		ws->push(starviosMsg(stranger));
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 3; }));
		QCOMPARE(rig.postedAt(2)["author"].toString(), QString("Espectador"));
		QCOMPARE(firstText(rig.postedAt(2)), QString("tres"));
		QVERIFY(!rig.socket(1));
	}

	void starviosDiscardedLookupsAreNotLost()
	{
		std::atomic<bool> release{false};
		StarviosRig rig;
		rig.extra = [&](const std::string &url, const std::string &) {
			net::Response res;
			if (url.find("/api/public/emotes") != std::string::npos) {
				// Mantiene ocupado el hilo de las consultas para que las siguientes queden en cola.
				StarviosRig::until([&] { return release.load(); }, 5000);
				res.status = 500;
			} else if (url.find("id=eq.n1") != std::string::npos) {
				res.status = 200;
				res.body = QJsonDocument(QJsonArray{chatRow("n1", "nuevo")}).toJson().toStdString();
			} else if (url.find("/rest/v1/live_chat_messages") != std::string::npos) {
				res.status = 200;
				res.body = "[]"; // las filas ya recordadas no vuelven en la consulta del hueco
			}
			return res;
		};
		rig.start();
		auto ws = rig.joined();
		QVERIFY(ws);
		ws->push(starviosMsg(chatRow("m0", "hola :abc_d:")));
		QVERIFY(StarviosRig::until([&] { return rig.requestCount("/api/public/emotes") == 1; }));
		// Tantas consultas en cola como admite el conector, y el socket se cae antes de hacerlas.
		for (int i = 0; i < 16; ++i) {
			QJsonObject row = chatRow(QStringLiteral("q%1").arg(i), QStringLiteral("en cola %1").arg(i), {}, false);
			row.insert("user_id", QStringLiteral("espectador%1").arg(i)); // nadie conocido: todos necesitan consulta
			ws->push(starviosMsg(row));
		}
		ws->push(starviosMsg(chatRow("marca", "marca")));
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 2; }));
		QCOMPARE(rig.requestCount("id=eq."), 0);
		ws->close();
		release = true;
		// Cada mensaje en cola se muestra con la etiqueta neutra, sin esperar a la consulta.
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 18; }));
		for (int i = 0; i < 16; ++i) {
			const QJsonObject message = rig.postedAt(2 + i);
			QCOMPARE(firstText(message), QStringLiteral("en cola %1").arg(i));
			QCOMPARE(message["author"].toString(), QString("Espectador"));
			QVERIFY(!message["recovered"].toBool());
		}
		QCOMPARE(rig.requestCount("id=eq."), 0);
		// Lo descartado no gasta el cupo: en la sesión siguiente se sigue preguntando por el autor.
		auto second = rig.joined(1);
		QVERIFY(second);
		QJsonObject fresh = chatRow("n1", "nuevo", {}, false);
		fresh.insert("user_id", "u99");
		second->push(starviosMsg(fresh));
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 19; }));
		QCOMPARE(rig.requestCount("id=eq.n1"), 1);
		QCOMPARE(rig.postedAt(18)["author"].toString(), QString("Ana"));
	}

	void starviosNothingIsPublishedAfterStop()
	{
		std::atomic<bool> release{false};
		StarviosRig rig;
		rig.extra = [&](const std::string &url, const std::string &) {
			net::Response res;
			if (url.find("id=eq.") == std::string::npos)
				return res;
			StarviosRig::until([&] { return release.load(); }, 5000);
			res.status = 200;
			res.body = QJsonDocument(QJsonArray{chatRow("m1", "uno")}).toJson().toStdString();
			return res;
		};
		rig.start();
		auto ws = rig.joined();
		QVERIFY(ws);
		// Una consulta en curso y otra en cola cuando se quita el chat.
		ws->push(starviosMsg(chatRow("m1", "uno", {}, false)));
		QVERIFY(StarviosRig::until([&] { return rig.requestCount("id=eq.m1") == 1; }));
		QJsonObject queued = chatRow("m2", "dos", {}, false);
		queued.insert("user_id", "u2");
		ws->push(starviosMsg(queued));
		ws->push(starviosMsg(chatRow("m3", "marca")));
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 1; }));
		rig.connector->stop();
		release = true;
		rig.connector->join();
		QCOMPARE(rig.postedCount(), 1);
	}

	void starviosReconnectRecoversGap()
	{
		StarviosRig rig;
		rig.extra = [&](const std::string &url, const std::string &) {
			net::Response res;
			if (url.find("/rest/v1/live_chat_messages") == std::string::npos)
				return res;
			res.status = 200;
			QJsonObject deleted = chatRow("m4", "borrado", "2026-01-02T03:04:07+00:00");
			deleted.insert("is_deleted", true);
			res.body = QJsonDocument(QJsonArray{chatRow("m1", "ya visto"),
							    chatRow("m2", "recuperado", "2026-01-02T03:04:06+00:00"),
							    deleted})
					   .toJson()
					   .toStdString();
			return res;
		};
		rig.start();
		auto ws = rig.joined();
		QVERIFY(ws);
		QCOMPARE(rig.requestCount("live_chat_messages"), 0);
		ws->push(starviosMsg(chatRow("m1", "vivo", "2026-01-02T03:04:05.100000+00:00")));
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 1; }));
		ws->close();
		auto second = rig.joined(1);
		QVERIFY(second);
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 2; }));
		const QJsonObject recovered = rig.postedAt(1);
		QCOMPARE(firstText(recovered), QString("recuperado"));
		QVERIFY(recovered["recovered"].toBool());
		QVERIFY(!rig.postedAt(0).contains("recovered"));
		const QString gap = rig.lastRequest("created_at=gt.");
		QVERIFY(gap.contains("stream_id=eq.s1"));
		QVERIFY(gap.contains("created_at=gt.2026-01-02T03%3A04%3A05.100000%2B00%3A00"));
		QVERIFY(gap.contains("limit=100"));
		QVERIFY(gap.contains("order=created_at.asc"));
		// Un directo nuevo no recupera lo del anterior: ni socket viejo ni historial.
		const int before = rig.requestCount("created_at=gt.");
		{
			std::lock_guard<std::mutex> lock(rig.mutex);
			rig.streamId = "s2";
		}
		second->close();
		auto third = rig.joined(2);
		QVERIFY(third);
		QVERIFY(StarviosRig::until([&] { return rig.hasStatus("connected"); }));
		const QJsonArray join = QJsonDocument::fromJson(third->sent().value(0).toUtf8()).array();
		QCOMPARE(join[2].toString(), QString("realtime:stream:s2"));
		QThread::msleep(50);
		QCOMPARE(rig.requestCount("created_at=gt."), before);
	}

	void starviosOfflineWaitsAndFollowsNewStream()
	{
		StarviosRig rig;
		rig.status = "ended";
		rig.start();
		QVERIFY(StarviosRig::until([&] { return rig.hasStatus("waiting"); }));
		QVERIFY(!rig.socket(0));
		{
			std::lock_guard<std::mutex> lock(rig.mutex);
			rig.status = "live";
		}
		QVERIFY(rig.joined());
	}

	void starviosStreamChangeMovesTheSocket()
	{
		StarviosRig rig;
		starvios::Timing t = StarviosRig::fastTiming();
		t.discoveryMs = 30;
		rig.start(t);
		auto ws = rig.joined();
		QVERIFY(ws);
		{
			std::lock_guard<std::mutex> lock(rig.mutex);
			rig.streamId = "s2";
		}
		auto second = rig.waitSocket(1);
		QVERIFY(second);
		QVERIFY(StarviosRig::until([&] { return !second->sent().isEmpty(); }));
		QCOMPARE(QJsonDocument::fromJson(second->sent().value(0).toUtf8()).array()[2].toString(),
			 QString("realtime:stream:s2"));
	}

	void starviosRecoversRotatedKey()
	{
		StarviosRig rig;
		rig.extra = [&](const std::string &url, const std::string &) {
			net::Response res;
			if (url == "https://starvios.com/") {
				res.status = 200;
				res.body = fixture(13);
			} else if (url == "https://starvios.com/assets/client-D8x_9-q.js") {
				res.status = 200;
				res.body = fixture(14);
			}
			return res;
		};
		rig.start();
		auto ws = rig.waitSocket(0);
		QVERIFY(ws);
		QVERIFY(StarviosRig::until([&] { return !ws->sent().isEmpty(); }));
		const QJsonArray join = QJsonDocument::fromJson(ws->sent().value(0).toUtf8()).array();
		ws->push(QJsonDocument(QJsonArray{join[0], join[1], join[2], "phx_reply",
						  QJsonObject{{"status", "error"}, {"response", QJsonObject{{"reason", "bad key"}}}}})
				 .toJson(QJsonDocument::Compact));
		auto second = rig.waitSocket(1);
		QVERIFY(second);
		QCOMPARE(QString::fromStdString(second->url),
			 QString("wss://abcdefgh.supabase.co/realtime/v1/websocket?apikey=sb_publishable_Nueva-Clave_123&vsn=2.0.0"));
		QCOMPARE(rig.requestCount("https://starvios.com/assets/client-"), 1);
	}

	void starviosRotationDoesNotLoop()
	{
		StarviosRig rig;
		rig.start();
		for (int i = 0; i < 2; ++i) {
			auto ws = rig.waitSocket(i);
			QVERIFY(ws);
			QVERIFY(StarviosRig::until([&] { return !ws->sent().isEmpty(); }));
			const QJsonArray join = QJsonDocument::fromJson(ws->sent().value(0).toUtf8()).array();
			ws->push(QJsonDocument(QJsonArray{join[0], join[1], join[2], "phx_reply", QJsonObject{{"status", "error"}}})
					 .toJson(QJsonDocument::Compact));
		}
		QVERIFY(rig.waitSocket(2));
		// Dos rechazos, pero la portada se consulta una sola vez; el error llega al usuario sin la clave.
		QVERIFY(rig.hasStatus("error:"));
		std::lock_guard<std::mutex> lock(rig.mutex);
		QCOMPARE(int(rig.requests.count("GET https://starvios.com/")), 1);
		for (const QString &s : rig.statuses)
			QVERIFY(!s.contains("sb_publishable_"));
	}

	void starviosEmotesLoadOnceWithoutBlocking()
	{
		StarviosRig rig;
		std::atomic<bool> release{false};
		rig.extra = [&](const std::string &url, const std::string &) {
			net::Response res;
			if (url == "https://starvios.com/api/public/emotes") {
				StarviosRig::until([&] { return release.load(); });
				res.status = 200;
				res.body = fixture(12);
			}
			return res;
		};
		rig.start();
		auto ws = rig.joined();
		QVERIFY(ws);
		ws->push(starviosMsg(chatRow("m0", "sin codigos")));
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 1; }));
		QCOMPARE(rig.requestCount("api/public/emotes"), 0);
		// El catálogo tarda, pero el mensaje no espera: de momento va como texto.
		ws->push(starviosMsg(chatRow("m1", "a :abc_d:")));
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 2; }));
		QCOMPARE(firstText(rig.postedAt(1)), QString("a :abc_d:"));
		ws->push(starviosMsg(chatRow("m2", "b :abc_d:")));
		QVERIFY(StarviosRig::until([&] { return rig.postedCount() == 3; }));
		release = true;
		QVERIFY(StarviosRig::until([&] {
			ws->push(starviosMsg(chatRow(QString::number(rig.postedCount() + 10), ":abc_d:")));
			return rig.postedAt(rig.postedCount() - 1)["parts"].toArray()[0].toObject()["type"].toString() == "emote";
		}));
		QCOMPARE(rig.requestCount("api/public/emotes"), 1);
	}

	void starviosCheckLiveReadsViewers()
	{
		StarviosRig rig;
		std::mutex bodyMutex;
		std::string body;
		rig.extra = [&](const std::string &url, const std::string &sent) {
			net::Response res;
			if (url.find("/rest/v1/rpc/live_viewer_counts") != std::string::npos) {
				std::lock_guard<std::mutex> lock(bodyMutex);
				body = sent;
				res.status = 200;
				res.body = "[{\"stream_id\":\"s1\",\"viewers\":42}]";
			}
			return res;
		};
		const LiveResult r = starvios::checkLive("ana_test");
		QCOMPARE(r.live, 1);
		QCOMPARE(r.viewers, 42);
		QCOMPARE(QJsonDocument::fromJson(QByteArray::fromStdString(body)).object()["_stream_ids"].toArray()[0].toString(),
			 QString("s1"));
		QCOMPARE(rig.lastRequest("discovery"),
			 QString("GET https://starvios.com/api/public/discovery?kind=channel&username=ana_test"));
		SourceInfo info;
		QString error;
		QVERIFY(parseEntry("starvios:ana_test", info, error));
		QCOMPARE(checkLive(info).live, 1);
		QCOMPARE(createConnector(info, {}, {}) != nullptr, true);
	}

	void parseEntries()
	{
		QCOMPARE(entry("twitch:MiCanal").key, QString("twitch:micanal"));
		QCOMPARE(entry("kick:Canal").channel, QString("canal"));
		QCOMPARE(entry("kick:canal#1234").extra, QString("1234"));
		QCOMPARE(entry("youtube:@Canal").channel, QString("@Canal"));
		QCOMPARE(entry("youtube:Canal").channel, QString("@Canal"));
		QCOMPARE(entry("yt:UCabcdefghijklmnopqrstuv").channel, QString("UCabcdefghijklmnopqrstuv"));
		QCOMPARE(entry("https://www.twitch.tv/MiCanal").channel, QString("micanal"));
		QCOMPARE(entry("https://kick.com/canal").platform, QString("kick"));
		QCOMPARE(entry("https://www.youtube.com/@Canal").channel, QString("@Canal"));
		SourceInfo v = entry("https://www.youtube.com/watch?v=dQw4w9WgXcQ");
		QCOMPARE(v.channel, QString("dQw4w9WgXcQ"));
		QVERIFY(v.videoFixed);
		QCOMPARE(entry("https://youtu.be/dQw4w9WgXcQ").extra, QString("dQw4w9WgXcQ"));
		QCOMPARE(entry("youtube.com/live/dQw4w9WgXcQ").extra, QString("dQw4w9WgXcQ"));
		QCOMPARE(entry("tiktok:@Mi.Canal").key, QString("tiktok:@mi.canal"));
		QCOMPARE(entry("tiktok:mi_canal").channel, QString("@mi_canal"));
		QCOMPARE(entry("https://www.tiktok.com/@MiCanal/live").channel, QString("@micanal"));

		SourceInfo s;
		QString error;
		for (const char *bad : {"canal", "facebook:x", "twitch:", "", "tiktok:mal nombre", "tiktok:@"})
			QVERIFY2(!parseEntry(bad, s, error), bad);
	}

	void twitchMessage()
	{
		const QString line = "@badge-info=subscriber/8;badges=broadcaster/1,subscriber/6;color=#0D4200;"
				     "display-name=Mi\\sCanal;emotes=25:0-4,11-15;id=abc-1 "
				     ":micanal!micanal@micanal.tmi.twitch.tv PRIVMSG #micanal :Kappa hola Kappa";
		twitch::IrcMessage irc = twitch::parseIrc(line);
		QCOMPARE(irc.command, QString("PRIVMSG"));
		QCOMPARE(irc.params, QStringList({"#micanal", "Kappa hola Kappa"}));
		QJsonObject m = twitch::toMessage(irc, "micanal");
		QCOMPARE(m["author"].toString(), QString("Mi Canal"));
		QCOMPARE(m["badges"].toArray(), QJsonArray({"owner", "subscriber"}));
		QJsonArray parts = m["parts"].toArray();
		QCOMPARE(parts.size(), 3);
		QCOMPARE(parts[0].toObject()["type"].toString(), QString("emote"));
		QCOMPARE(parts[1].toObject()["text"].toString(), QString(" hola "));
		QCOMPARE(m["id"].toString(), QString("abc-1"));
	}

	void twitchUnicodeAndAction()
	{
		// El emoji ocupa 2 unidades UTF-16 pero 1 punto de código: Kappa empieza en 2.
		twitch::IrcMessage irc = twitch::parseIrc(QString::fromUtf8("@emotes=25:2-6 :u!u@u PRIVMSG #c :\x01"
									    "ACTION 😀 Kappa\x01"));
		QJsonObject m = twitch::toMessage(irc, "c");
		QVERIFY(m["action"].toBool());
		QJsonArray parts = m["parts"].toArray();
		QCOMPARE(parts.size(), 2);
		QCOMPARE(parts[0].toObject()["text"].toString(), QString::fromUtf8("😀 "));
		QCOMPARE(parts[1].toObject()["name"].toString(), QString("Kappa"));
	}

	void twitchUserNoticeAndPing()
	{
		QJsonObject m = twitch::toMessage(
			twitch::parseIrc(
				"@display-name=Fan;msg-id=resub;system-msg=Fan\\ssubscribed\\sfor\\s3\\smonths "
				":tmi.twitch.tv USERNOTICE #c :gracias!"),
			"c");
		QCOMPARE(m["kind"].toString(), QString("highlight"));
		QCOMPARE(m["system"].toString(), QString("Fan subscribed for 3 months"));
		QCOMPARE(twitch::parseIrc("PING :tmi.twitch.tv").params, QStringList({"tmi.twitch.tv"}));
		QVERIFY(twitch::toMessage(twitch::parseIrc(":tmi.twitch.tv 366 justinfan #c :End"), "c").isEmpty());
	}

	void kickMessage()
	{
		QJsonArray parts = kick::splitContent("hola [emote:37226:KEKW] que tal");
		QCOMPARE(parts.size(), 3);
		QCOMPARE(parts[1].toObject()["url"].toString(),
			 QString("https://files.kick.com/emotes/37226/fullsize"));
		QJsonObject data = QJsonDocument::fromJson(fixture(0)).object();
		QJsonObject m = kick::toMessage(data, "canal");
		QCOMPARE(m["author"].toString(), QString("pepe"));
		QCOMPARE(m["color"].toString(), QString("#FF0000"));
		QCOMPARE(m["badges"].toArray(), QJsonArray({"moderator", "vip"}));
	}

	void youtubeMessage()
	{
		QJsonObject action = QJsonDocument::fromJson(fixture(1)).object();
		QJsonObject m = youtube::toMessage(action, "@canal");
		QCOMPARE(m["author"].toString(), QString("Ana"));
		QCOMPARE(m["amount"].toString(), QString("$5.00"));
		QCOMPARE(m["kind"].toString(), QString("highlight"));
		QCOMPARE(m["badges"].toArray(), QJsonArray({"moderator"}));
		QJsonArray parts = m["parts"].toArray();
		QCOMPARE(parts[1].toObject()["text"].toString(), QString::fromUtf8("😀"));
		QCOMPARE(parts[2].toObject()["type"].toString(), QString("emote"));

		int timeout = 0;
		QJsonObject lc = QJsonDocument::fromJson(fixture(2)).object();
		QCOMPARE(youtube::nextContinuation(lc, timeout), QString("abc"));
		QCOMPARE(timeout, 4000);
	}

	void youtubeLivePage()
	{
		const QString live = fixture(3);
		QCOMPARE(youtube::liveVideoFromPage(live), QString("dQw4w9WgXcQ"));
		QVERIFY(youtube::liveVideoFromPage(fixture(4)).isEmpty());
	}
	void youtubeViewers()
	{
		QCOMPARE(youtube::viewersFromPage(fixture(5)), 1234);
		QCOMPARE(youtube::viewersFromPage(fixture(6)), 1234);
		QCOMPARE(youtube::viewersFromPage(fixture(7)), -1); // vídeo grabado
		QCOMPARE(youtube::viewersFromPage("nada"), -1);
	}

	void tiktokFetchResult()
	{
		using tiktok::ProtoWriter;
		const QByteArray user = ProtoWriter().bytes(3, "Ana 🌸").bytes(38, "ana.tok").data();
		const QByteArray chatMsg = ProtoWriter()
						   .bytes(2, user)
						   .bytes(3, "hola desde TikTok")
						   .bytes(18, ProtoWriter().varint(5, 1).data())
						   .data();
		const QByteArray gift = ProtoWriter()
						.bytes(15, ProtoWriter().varint(11, 1).varint(12, 1).bytes(16, "Rosa").data())
						.varint(5, 5)
						.bytes(7, user)
						.data();
		const QByteArray giftEnd = gift + ProtoWriter().varint(9, 1).data();
		auto event = [](const char *method, const QByteArray &payload) {
			return ProtoWriter().bytes(1, method).bytes(2, payload).data();
		};
		const QByteArray data = ProtoWriter()
						.bytes(1, event("WebcastChatMessage", chatMsg))
						.bytes(1, event("WebcastGiftMessage", gift))
						.bytes(1, event("WebcastGiftMessage", giftEnd))
						.bytes(1, event("WebcastMemberMessage", "x"))
						.bytes(5, "ext")
						.bytes(7, ProtoWriter().bytes(1, "cursor").bytes(2, "c1").data())
						.varint(9, 1)
						.bytes(10, "wss://webcast/ws")
						.data();

		tiktok::FetchResult r = tiktok::parseFetchResult(data);
		QCOMPARE(r.events.size(), 4);
		QCOMPARE(r.internalExt, QString("ext"));
		QCOMPARE(r.pushServer, QString("wss://webcast/ws"));
		QVERIFY(r.needAck);
		QCOMPARE(r.routeParams.size(), 1);
		QCOMPARE(r.routeParams[0].first, QString("cursor"));

		QJsonObject m = tiktok::toMessage(r.events[0], "@canal");
		QCOMPARE(m["platform"].toString(), QString("tiktok"));
		QCOMPARE(m["author"].toString(), QString::fromUtf8("Ana 🌸"));
		QCOMPARE(m["parts"].toArray()[0].toObject()["text"].toString(), QString("hola desde TikTok"));
		QCOMPARE(m["badges"].toArray(), QJsonArray({"moderator"}));
		QVERIFY(tiktok::toMessage(r.events[1], "@canal").isEmpty()); // regalo en racha
		QJsonObject g = tiktok::toMessage(r.events[2], "@canal");
		QCOMPARE(g["kind"].toString(), QString("highlight"));
		QVERIFY(g["system"].toString().contains(QString::fromUtf8("5× Rosa")));
		QCOMPARE(g["amount"].toString(), QString::fromUtf8("5 💎"));
		QVERIFY(tiktok::toMessage(r.events[3], "@canal").isEmpty());

		// Datos truncados o basura: no debe colgarse ni salirse del búfer.
		for (int cut = 0; cut < data.size(); ++cut)
			tiktok::parseFetchResult(QByteArrayView(data).first(cut));
		tiktok::parseFetchResult(QByteArray("\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff", 11));
	}

	void tiktokRoom()
	{
		QString room;
		LiveResult r = tiktok::parseRoom(
			QJsonDocument::fromJson("{\"data\":{\"user\":{\"roomId\":\"7311\"},\"liveRoom\":{\"status\":2,"
						"\"liveRoomStats\":{\"userCount\":321}}}}")
				.object(),
			room);
		QCOMPARE(room, QString("7311"));
		QCOMPARE(r.live, 1);
		QCOMPARE(r.viewers, 321);
		r = tiktok::parseRoom(QJsonDocument::fromJson("{\"data\":{\"user\":{\"roomId\":\"7311\"},"
							      "\"liveRoom\":{\"status\":4}}}")
					      .object(),
				      room);
		QCOMPARE(r.live, 0);
		QCOMPARE(r.viewers, -1);
	}
	void links()
	{
		QCOMPARE(linkifyHtml("hola <b>"), QString("hola &lt;b&gt;"));
		QCOMPARE(linkifyHtml("mira https://kick.com/canal."),
			 QString("mira <a href=\"https://kick.com/canal\">https://kick.com/canal</a>."));
		QCOMPARE(linkifyHtml("(www.youtube.com/@x)"),
			 QString("(<a href=\"https://www.youtube.com/@x\">www.youtube.com/@x</a>)"));
		QCOMPARE(linkifyHtml("https://es.wikipedia.org/wiki/OBS_(software)"),
			 QString("<a href=\"https://es.wikipedia.org/wiki/OBS_(software)\">"
				 "https://es.wikipedia.org/wiki/OBS_(software)</a>"));
		QCOMPARE(linkifyHtml("a&b http://x.io/?a=1&b=2", "#00f"),
			 QString("a&amp;b <a href=\"http://x.io/?a=1&amp;b=2\" style=\"color:#00f;\">"
				 "http://x.io/?a=1&amp;b=2</a>"));
		QVERIFY(!linkifyHtml("javascript:alert(1) ftp://x").contains("<a"));
	}
	void profiles()
	{
		Profile y = youtube::profileFromPage(fixture(8));
		QCOMPARE(y.name, QString("Alejo & Tech"));
		QCOMPARE(y.avatar, QString("https://yt3.googleusercontent.com/abc123=s88-c-k-c0x00ffffff-no-rp"));
		Profile v = youtube::profileFromPage(fixture(9));
		QCOMPARE(v.name, QString("Mi Canal"));
		QCOMPARE(v.avatar, QString("https://yt3.ggpht.com/xyz=s88-c-k-c0x00ffffff-no-rp"));

		Profile t = twitch::profileFromGql(QJsonDocument::fromJson(
			"{\"data\":{\"user\":{\"displayName\":\"MiCanal\",\"profileImageURL\":\"https://static-cdn.jtvnw.net/a.png\"}}}").object());
		QCOMPARE(t.name, QString("MiCanal"));
		QCOMPARE(t.avatar, QString("https://static-cdn.jtvnw.net/a.png"));

		Profile k = kick::profileFromChannel(QJsonDocument::fromJson(
			"{\"user\":{\"username\":\"alejoxcode\",\"profile_pic\":\"https://files.kick.com/p.webp\"}}").object());
		QCOMPARE(k.name, QString("alejoxcode"));
		QCOMPARE(k.avatar, QString("https://files.kick.com/p.webp"));

		Profile tt = tiktok::profileFromRoom(QJsonDocument::fromJson(
			"{\"data\":{\"user\":{\"nickname\":\"Alejo\",\"avatarThumb\":\"https://p16.tiktokcdn.com/a.jpeg\"}}}").object());
		QCOMPARE(tt.name, QString("Alejo"));
		QCOMPARE(tt.avatar, QString("https://p16.tiktokcdn.com/a.jpeg"));
	}
	void updates()
	{
		QVERIFY(isNewerVersion("v2.0.2", "2.0.1"));
		QVERIFY(isNewerVersion("2.0.10", "2.0.9"));
		QVERIFY(isNewerVersion("2.1", "2.0.9"));
		QVERIFY(!isNewerVersion("v2.0.1", "2.0.1"));
		QVERIFY(!isNewerVersion("2.0.0", "2.0.1"));
		QVERIFY(!isNewerVersion("", "2.0.1"));
		const QJsonObject json = QJsonDocument::fromJson(
			"{\"tag_name\":\"v2.0.2\",\"html_url\":\"https://github.com/r/releases/tag/v2.0.2\",\"assets\":["
			"{\"name\":\"obs-multichat-2.0.2-macos.zip\",\"browser_download_url\":\"https://d/mac.zip\"},"
			"{\"name\":\"obs-multichat-2.0.2-windows-x64.zip\",\"browser_download_url\":\"https://d/win.zip\"}]}").object();
		Release r = parseLatestRelease(json, "-windows-x64.zip");
		QCOMPARE(r.version, QString("2.0.2"));
		QCOMPARE(r.url, QString("https://d/win.zip"));
		QCOMPARE(parseLatestRelease(json, "-linux.zip").url, QString("https://github.com/r/releases/tag/v2.0.2"));
		QJsonObject draft = json;
		draft.insert("prerelease", true);
		QVERIFY(parseLatestRelease(draft, "-macos.zip").version.isEmpty());
	}
};

QTEST_GUILESS_MAIN(ChatTest)
#include "test-chat.moc"
