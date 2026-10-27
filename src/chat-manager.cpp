#include "chat-manager.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>

#include <algorithm>

namespace {

constexpr int kHistory = 200;
// Servicios de OBS que ya indican multistream (transmiten a varias plataformas a la vez).
const char *const kMultiServices[] = {"restream", "castr", "streamyard", "multistream", "prism"};

qint64 nowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

QString configPath()
{
	char *path = obs_module_config_path("config.json");
	QString out = QString::fromUtf8(path);
	bfree(path);
	return out;
}

int platformOrder(const QString &p)
{
	static const QStringList order = {"twitch", "youtube", "kick", "demo"};
	return order.indexOf(p);
}

} // namespace

ChatManager &ChatManager::instance()
{
	static ChatManager *manager = new ChatManager(); // vive hasta que OBS cierra
	return *manager;
}

ChatManager::ChatManager()
{
	pool_.setMaxThreadCount(4);
	timer_.setInterval(2000);
	connect(&timer_, &QTimer::timeout, this, &ChatManager::tick);
	timer_.start();
}

void ChatManager::load()
{
	QFile file(configPath());
	QJsonObject cfg;
	if (file.open(QIODevice::ReadOnly))
		cfg = QJsonDocument::fromJson(file.readAll()).object();
	QStringList list;
	for (const QJsonValue &v : cfg.value("chats").toArray())
		list << v.toString();
	configure(list, cfg.value("mode").toString() == "always" ? Mode::Always : Mode::Auto, false);
}

void ChatManager::save() const
{
	const QString path = configPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QJsonObject cfg{{"chats", QJsonArray::fromStringList(entries_)},
			{"mode", mode_ == Mode::Always ? "always" : "auto"}};
	QFile file(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		file.write(QJsonDocument(cfg).toJson());
	else
		obs_log(LOG_WARNING, "no se pudo guardar %s", path.toUtf8().constData());
}

QStringList ChatManager::configure(const QStringList &entries, Mode mode, bool demo)
{
	std::vector<chat::SourceInfo> wanted;
	QStringList cleanEntries, errors;
	auto add = [&](const QString &entry) {
		chat::SourceInfo info;
		QString error;
		if (!chat::parseEntry(entry, info, error)) {
			errors << error;
			return;
		}
		for (const auto &w : wanted)
			if (w.key == info.key)
				return;
		wanted.push_back(info);
	};
	for (const QString &e : entries) {
		if (e.trimmed().isEmpty())
			continue;
		cleanEntries << e.trimmed();
		add(e);
	}
	if (demo)
		add("demo:demo");

	// Quita los que ya no están y añade los nuevos; los que siguen no se tocan.
	for (auto it = sources_.begin(); it != sources_.end();) {
		bool keep = false;
		for (const auto &w : wanted)
			keep = keep || w.key == (*it)->info.key;
		if (keep) {
			++it;
		} else {
			disconnectSource(**it);
			it = sources_.erase(it);
		}
	}
	for (const auto &w : wanted) {
		bool exists = false;
		for (const auto &s : sources_)
			exists = exists || s->info.key == w.key;
		if (!exists) {
			auto s = std::make_unique<Source>();
			s->info = w;
			sources_.push_back(std::move(s));
		}
	}

	entries_ = cleanEntries;
	errors_ = errors;
	mode_ = mode;
	demo_ = demo;
	tick();
	return errors;
}

void ChatManager::setStreaming(bool active, const QString &service)
{
	streaming_ = active;
	service_ = active ? service : QString();
	if (active) {
		// Las plataformas tardan en marcar el canal como en vivo: durante 3 minutos
		// lo comprobamos cada 15 s en lugar de cada minuto.
		boostUntil_ = nowMs() + 180000;
		for (auto &s : sources_)
			s->nextCheck = 0;
	}
	tick();
}

void ChatManager::refresh()
{
	for (auto &s : sources_)
		s->nextCheck = 0;
	tick();
}

void ChatManager::clear()
{
	history_ = QJsonArray();
	emit cleared();
}

bool ChatManager::wants(const Source &s) const
{
	if (mode_ == Mode::Always || s.info.platform == "demo")
		return true;
	return s.live == 1 || (s.live == -1 && streaming_);
}

ChatManager::Source *ChatManager::find(const QString &key, quint64 generation)
{
	for (auto &s : sources_)
		if (s->info.key == key && (generation == 0 || s->generation == generation))
			return s.get();
	return nullptr;
}

void ChatManager::connectSource(Source &s)
{
	const QString key = s.info.key;
	const quint64 gen = s.generation = nextGeneration_++;
	QPointer<ChatManager> self(this);

	auto post = [self, key](QJsonObject msg) {
		msg.insert("source", key);
		QMetaObject::invokeMethod(
			self,
			[self, msg] {
				if (!self)
					return;
				self->history_.append(msg);
				while (self->history_.size() > kHistory)
					self->history_.removeFirst();
				emit self->message(msg);
			},
			Qt::QueuedConnection);
	};
	auto report = [self, key, gen](const QString &status, const QString &detail) {
		QMetaObject::invokeMethod(
			self,
			[self, key, gen, status, detail] {
				if (!self)
					return;
				if (Source *src = self->find(key, gen)) {
					src->status = status;
					src->detail = detail;
					if (status == "error")
						obs_log(LOG_INFO, "[%s] %s", key.toUtf8().constData(),
							detail.toUtf8().constData());
					self->publishState();
				}
			},
			Qt::QueuedConnection);
	};

	s.status = "connecting";
	s.detail.clear();
	s.connector = chat::createConnector(s.info, post, report);
	s.connector->start();
}

void ChatManager::disconnectSource(Source &s)
{
	if (s.connector) {
		s.connector->stop();
		retired_.push_back(std::move(s.connector));
		s.connector.reset();
	}
	s.generation = 0;
	s.status = "idle";
	s.detail.clear();
}

void ChatManager::reapRetired(bool wait)
{
	for (auto it = retired_.begin(); it != retired_.end();) {
		if (wait || (*it)->finished()) {
			(*it)->join();
			it = retired_.erase(it);
		} else {
			++it;
		}
	}
}

void ChatManager::tick()
{
	if (shutdown_)
		return;
	const qint64 now = nowMs();
	const qint64 interval = now < boostUntil_ ? 15000 : 60000;
	QPointer<ChatManager> self(this);

	for (auto &sp : sources_) {
		Source &s = *sp;
		if (!s.checking && now >= s.nextCheck) {
			s.checking = true;
			s.nextCheck = now + interval;
			const chat::SourceInfo info = s.info;
			pool_.start([self, info] {
				chat::LiveResult r = chat::checkLive(info);
				QMetaObject::invokeMethod(
					self,
					[self, info, r] {
						if (!self)
							return;
						if (Source *src = self->find(info.key, 0)) {
							src->checking = false;
							src->live = r.live;
							if (src->info.platform == "youtube" && !src->info.videoFixed)
								src->info.extra = r.videoId;
							self->tick();
						}
					},
					Qt::QueuedConnection);
			});
		}
		const bool want = wants(s);
		if (want && !s.connector)
			connectSource(s);
		else if (!want && s.connector)
			disconnectSource(s);
	}
	reapRetired(false);
	publishState();
}

QJsonObject ChatManager::state() const
{
	QJsonArray sources;
	QStringList live;
	for (const auto &s : sources_) {
		sources.append(QJsonObject{
			{"key", s->info.key},
			{"platform", s->info.platform},
			{"channel", s->info.channel},
			{"live", s->live},
			{"status", s->status},
			{"detail", s->detail},
		});
		if (s->live == 1 && s->info.platform != "demo" && !live.contains(s->info.platform))
			live << s->info.platform;
	}
	std::sort(live.begin(), live.end(),
		  [](const QString &a, const QString &b) { return platformOrder(a) < platformOrder(b); });
	bool multiService = false;
	for (const char *word : kMultiServices)
		multiService = multiService || service_.contains(QLatin1String(word), Qt::CaseInsensitive);
	return QJsonObject{
		{"mode", mode_ == Mode::Always ? "always" : "auto"},
		{"streaming", streaming_},
		{"service", service_},
		{"multistream", live.size() > 1 || (streaming_ && multiService)},
		{"livePlatforms", QJsonArray::fromStringList(live)},
		{"sources", sources},
		{"errors", QJsonArray::fromStringList(errors_)},
	};
}

QJsonArray ChatManager::history() const
{
	return history_;
}

void ChatManager::publishState()
{
	QJsonObject st = state();
	if (st != lastState_) {
		lastState_ = st;
		emit stateChanged(st);
	}
}

void ChatManager::shutdown()
{
	if (shutdown_)
		return;
	shutdown_ = true;
	timer_.stop();
	for (auto &s : sources_)
		disconnectSource(*s);
	// Los hilos no pueden seguir vivos cuando OBS descarga el plugin.
	reapRetired(true);
	pool_.waitForDone();
}
