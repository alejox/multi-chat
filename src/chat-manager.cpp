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
	static const QStringList order = {"twitch", "youtube", "kick", "tiktok", "starvios", "demo"};
	return int(order.indexOf(p));
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
	config_ = cfg;
	filters_ = chat::FilterRules::fromJson(cfg.value("filters").toObject());
	configure(chat::chatEntriesFromJson(cfg), cfg.value("mode").toString() == "always" ? Mode::Always : Mode::Auto,
		  false);
}

void ChatManager::save() const
{
	const QString path = configPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QJsonObject cfg = config_;
	chat::chatEntriesToJson(chats_, cfg);
	cfg.insert("mode", mode_ == Mode::Always ? "always" : "auto");
	cfg.insert("filters", filters_.toJson());
	QFile file(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		file.write(QJsonDocument(cfg).toJson());
	else
		obs_log(LOG_WARNING, "no se pudo guardar %s", path.toUtf8().constData());
}

void ChatManager::setWizardShown()
{
	if (wizardShown())
		return;
	config_.insert("wizardShown", true);
	save();
}

QStringList ChatManager::configure(const QList<chat::ChatEntry> &chats, Mode mode, bool demo)
{
	QList<chat::ChatEntry> clean;
	for (chat::ChatEntry e : chats) {
		e.entry = e.entry.trimmed();
		if (!e.entry.isEmpty())
			clean << e;
	}
	const chat::SourcePlan plan = chat::planSources(clean, demo);
	const std::vector<chat::SourceInfo> &wanted = plan.sources;
	QStringList errors;
	for (const chat::EntryProblem &p : plan.errors)
		errors << QString::fromUtf8(obs_module_text(chat::entryErrorKey(p.code))).arg(p.arg);

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
	// Same order as the list (the dock shows its chips in this order).
	std::vector<std::unique_ptr<Source>> ordered;
	for (const auto &w : wanted) {
		auto it = std::find_if(sources_.begin(), sources_.end(),
				       [&w](const std::unique_ptr<Source> &s) { return s && s->info.key == w.key; });
		if (it != sources_.end()) {
			ordered.push_back(std::move(*it));
		} else {
			auto s = std::make_unique<Source>();
			s->info = w;
			ordered.push_back(std::move(s));
		}
	}
	sources_ = std::move(ordered);

	chats_ = clean;
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
	clearedHistory_ = history_;
	clearedLastFeatured_ = lastFeatured_;
	history_ = QJsonArray();
	emit cleared();
	// Al limpiar el chat tampoco se puede volver a mostrar el último destacado.
	lastFeatured_ = QJsonObject();
	clearFeatured();
}

bool ChatManager::undoClear()
{
	if (clearedHistory_.isEmpty())
		return false;
	// Messages that arrived after clearing stay, after the restored ones.
	QJsonArray merged = clearedHistory_;
	for (const QJsonValue v : history_)
		merged.append(v);
	while (merged.size() > kHistory)
		merged.removeFirst();
	history_ = merged;
	if (lastFeatured_.isEmpty())
		lastFeatured_ = clearedLastFeatured_;
	discardUndo();
	emit restored();
	return true;
}

void ChatManager::discardUndo()
{
	clearedHistory_ = QJsonArray();
	clearedLastFeatured_ = QJsonObject();
}

void ChatManager::hideMessage(const QJsonObject &message)
{
	const QJsonObject deletion = chat::hideMessage(message);
	if (!deletion.isEmpty())
		applyDeletion(deletion);
}

void ChatManager::hideUser(const QJsonObject &message)
{
	const QString author = message.value("author").toString().trimmed();
	if (author.isEmpty())
		return;
	const QString folded = chat::foldText(author);
	const bool known = std::any_of(filters_.hiddenUsers.cbegin(), filters_.hiddenUsers.cend(),
				       [&folded](const QString &u) { return chat::foldText(u.trimmed()) == folded; });
	if (!known) {
		filters_.hiddenUsers.append(author);
		save();
		emit filtersChanged();
	}
	const QJsonObject deletion = chat::hideAuthor(message);
	if (!deletion.isEmpty())
		applyDeletion(deletion);
}

bool ChatManager::feature(const QString &id)
{
	if (id.isEmpty())
		return false;
	for (qsizetype i = history_.size() - 1; i >= 0; --i) {
		const QJsonObject m = history_.at(i).toObject();
		if (m.value("id").toString() == id) {
			feature(m);
			return true;
		}
	}
	return false;
}

void ChatManager::feature(const QJsonObject &message)
{
	if (message.isEmpty()) {
		clearFeatured();
		return;
	}
	featured_ = message;
	lastFeatured_ = message;
	featuredAt_ = nowMs();
	emit featuredChanged(featured_);
}

void ChatManager::clearFeatured()
{
	if (featured_.isEmpty())
		return;
	featured_ = QJsonObject();
	featuredAt_ = nowMs();
	emit featuredChanged(featured_);
}

void ChatManager::reshowLast()
{
	if (!lastFeatured_.isEmpty())
		feature(QJsonObject(lastFeatured_));
}

void ChatManager::receive(const QJsonObject &object)
{
	if (chat::isDeletion(object)) {
		applyDeletion(object);
		return;
	}
	if (chat::isFiltered(object, filters_))
		return;
	history_.append(object);
	while (history_.size() > kHistory)
		history_.removeFirst();
	emit message(object);
}

void ChatManager::applyDeletion(QJsonObject deletion)
{
	QJsonArray ids;
	for (qsizetype i = history_.size() - 1; i >= 0; --i) {
		const QJsonObject m = history_.at(i).toObject();
		if (chat::deletionMatches(deletion, m)) {
			ids.append(m.value("id"));
			history_.removeAt(i);
		}
	}
	deletion.insert("ids", ids);
	// What "undo clear" would bring back must not include moderated messages either.
	for (qsizetype i = clearedHistory_.size() - 1; i >= 0; --i)
		if (chat::deletionMatches(deletion, clearedHistory_.at(i).toObject()))
			clearedHistory_.removeAt(i);
	if (!clearedLastFeatured_.isEmpty() && chat::deletionMatches(deletion, clearedLastFeatured_))
		clearedLastFeatured_ = QJsonObject();
	// A moderated message cannot stay on screen nor come back with "show the last one again".
	if (!lastFeatured_.isEmpty() && chat::deletionMatches(deletion, lastFeatured_))
		lastFeatured_ = QJsonObject();
	if (!featured_.isEmpty() && chat::deletionMatches(deletion, featured_))
		clearFeatured();
	emit deleted(deletion);
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
				if (self)
					self->receive(msg);
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
	QPointer<ChatManager> self(this);

	for (auto &sp : sources_) {
		Source &s = *sp;
		if (!s.checking && now >= s.nextCheck) {
			// En vivo se consulta cada 30 s para tener los espectadores al día.
			s.checking = true;
			s.nextCheck = now + (now < boostUntil_ ? 15000 : s.live == 1 ? 30000 : 60000);
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
							src->viewers = r.live == 1 ? r.viewers : -1;
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
	int viewers = -1;
	for (const auto &s : sources_) {
		if (s->viewers >= 0 && s->info.platform != "demo")
			viewers = std::max(viewers, 0) + s->viewers;
		sources.append(QJsonObject{
			{"key", s->info.key},
			{"platform", s->info.platform},
			{"channel", s->info.channel},
			{"live", s->live},
			{"viewers", s->viewers},
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
		{"viewers", viewers},
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
