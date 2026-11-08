// Shared state of the Multichat sources and what the chat manager sends to their pages.

#include "source-internal.hpp"

namespace overlay_source {

// OBS puede crear y destruir fuentes desde otros hilos: el acceso a la lista va con candado.
// El candado solo cubre la lista (y lastSources): nunca se mantiene durante llamadas a OBS que
// toman los candados de escenas o fuentes, porque OBS llama a ov_destroy con esos candados
// tomados y se bloquearían mutuamente.
std::mutex sourcesMutex;
std::set<OverlaySource *> sources;
// Con OBS transmitiendo nunca se muestran los mensajes de ejemplo.
std::atomic<bool> streaming{false};
// ChatManager::testMessages(): the pages show sample messages while there is no chat.
std::atomic<bool> sampleMessages{false};

void send(OverlaySource *s, const QJsonObject &payload)
{
	if (!s->browser)
		return;
	const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_string(&cd, "eventName", "multichat");
	calldata_set_string(&cd, "jsonString", json.constData());
	proc_handler_call(obs_source_get_proc_handler(s->browser), "javascript_event", &cd);
	calldata_free(&cd);
}

// Últimos canales conocidos (live y espectadores), para las fuentes de espectadores.
// Va con sourcesMutex.
QJsonArray lastSources;

QJsonObject configOf(const OverlaySource *s)
{
	std::lock_guard<std::mutex> lock(s->mutex);
	return s->config;
}

// Último estado del destacado, para mandarlo a las fuentes nuevas y tras recargar la página.
// Se escribe en el hilo de la interfaz y se lee desde donde OBS cree las fuentes.
std::mutex featuredMutex;
QJsonObject featuredState{{"type", "featured"}, {"message", QJsonValue::Null}, {"at", 0}};

// The manager decides when the message hides (it sends an empty one); "at" tells the page
// apart two consecutive features of the same message.
void sendFeatured(OverlaySource *s)
{
	QJsonObject payload;
	{
		std::lock_guard<std::mutex> lock(featuredMutex);
		payload = featuredState;
	}
	send(s, payload);
}

void sendAll(const QJsonObject &payload)
{
	forEachSource([&](OverlaySource *s) {
		if (s->kind == Kind::Chat)
			send(s, payload);
	});
}

// ---- Fuente de espectadores ----

void refresh(OverlaySource *s)
{
	if (s->kind == Kind::Viewers)
		refreshViewers(s);
	else if (s->kind == Kind::Clock)
		refreshClock(s);
}

void sendConfig(OverlaySource *s)
{
	QJsonObject config = configOf(s);
	config.insert("streaming", streaming.load());
	config.insert("sample", sampleMessages.load());
	send(s, QJsonObject{{"type", "config"}, {"config", config}});
}

// Forwards the chat manager's events to the pages and resends the config every few seconds.
void connectManager()
{
	ChatManager &manager = ChatManager::instance();
	sampleMessages = manager.testMessages();
	QObject::connect(&manager, &ChatManager::testMessagesChanged, &manager, [](bool on) {
		sampleMessages = on;
		forEachSource([](OverlaySource *s) {
			sendConfig(s);
			refresh(s); // the viewers box shows or drops its sample numbers
		});
	});
	QObject::connect(&manager, &ChatManager::message, &manager,
			 [](const QJsonObject &m) { sendAll(QJsonObject{{"type", "message"}, {"message", m}}); });
	QObject::connect(&manager, &ChatManager::cleared, &manager, [] { sendAll(QJsonObject{{"type", "clear"}}); });
	// Undo after clearing: the chat sources get the history back with their own event, not
	// through "message", so the voice source (which listens to ChatManager::message) does
	// not read them again. The page keeps at most its own limit anyway.
	QObject::connect(&manager, &ChatManager::restored, &manager, [] {
		const QJsonArray history = ChatManager::instance().history();
		QJsonArray recent;
		for (qsizetype i = std::max<qsizetype>(0, history.size() - 50); i < history.size(); ++i)
			recent.append(history.at(i));
		sendAll(QJsonObject{{"type", "restore"}, {"messages", recent}});
	});
	// The deletion event already has type "delete", source and the ids to remove.
	QObject::connect(&manager, &ChatManager::deleted, &manager, [](const QJsonObject &d) { sendAll(d); });
	auto pushViewers = [] {
		const QJsonArray list = ChatManager::instance().state().value("sources").toArray();
		{
			std::lock_guard<std::mutex> lock(sourcesMutex);
			lastSources = list;
		}
		forEachSource([](OverlaySource *s) {
			if (s->kind == Kind::Viewers)
				refreshViewers(s);
		});
	};
	QObject::connect(&manager, &ChatManager::stateChanged, &manager, pushViewers);
	QObject::connect(&manager, &ChatManager::featuredChanged, &manager, [](const QJsonObject &m) {
		{
			std::lock_guard<std::mutex> lock(featuredMutex);
			featuredState.insert("message", m.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m));
			featuredState.insert("at", ChatManager::instance().featuredAt());
		}
		forEachSource([](OverlaySource *s) {
			if (s->kind == Kind::Featured)
				sendFeatured(s);
		});
	});

	// La página puede recargarse (al cambiar el tamaño, al volver a la escena...):
	// le reenviamos la configuración cada pocos segundos.
	auto *heartbeat = new QTimer(&manager);
	heartbeat->setInterval(3000);
	QObject::connect(heartbeat, &QTimer::timeout, &manager, [] {
		forEachSource([](OverlaySource *s) {
			sendConfig(s);
			if (s->kind == Kind::Featured)
				sendFeatured(s);
		});
	});
	QObject::connect(heartbeat, &QTimer::timeout, &manager, pushViewers);
	// La cinta horizontal sigue al lienzo si cambia su resolución.
	QObject::connect(heartbeat, &QTimer::timeout, &manager, [] {
		forEachSource([](OverlaySource *s) {
			const QJsonObject config = configOf(s);
			if (fitsCanvas(s->kind, config) && s->width != tickerWidth(config)) {
				s->width = tickerWidth(config);
				resizeBrowser(s);
			}
		});
	});
	heartbeat->start();
}

} // namespace overlay_source
