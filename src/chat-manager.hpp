#pragma once

// Gestiona los chats configurados: detecta qué plataformas están en vivo (multistream),
// conecta y desconecta los chats y reparte los mensajes al dock y al overlay.
// Vive en el hilo principal de Qt; los conectores y las consultas corren en hilos aparte.

#include "chat/message.hpp"
#include "chat/platforms.hpp"
#include "config-store.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

class ChatManager : public QObject {
	Q_OBJECT

public:
	enum class Mode { Auto, Always };

	struct Source {
		chat::SourceInfo info;
		int live = -1;           // 1 en vivo, 0 offline, -1 no se sabe
		int viewers = -1;        // espectadores ahora mismo, -1 si no se sabe
		QString status = "idle"; // idle | connecting | waiting | connected | error
		QString detail;
		std::shared_ptr<chat::Connector> connector;
		quint64 generation = 0; // descarta avisos de conectores ya parados
		qint64 nextCheck = 0;
		bool checking = false;
	};

	// Everything the manager needs from outside: OBS in the plugin (see plugin-main.cpp), fakes in
	// tests. Empty hooks fall back to the defaults noted on each one.
	struct Environment {
		ConfigStore store;          // config.json; an empty path loads nothing and cannot save
		chat::Translator translate; // locale lookup (returns the key when missing); empty = English
		// One log line; warning = true for problems. Empty = no logging.
		std::function<void(bool warning, const QString &line)> log;
		// Empty = chat::createConnector.
		std::function<std::shared_ptr<chat::Connector>(const chat::SourceInfo &, chat::Connector::PostFn,
								 chat::Connector::ReportFn)>
			createConnector;
		// Runs on a worker thread. Empty = chat::checkLive.
		std::function<chat::LiveResult(const chat::SourceInfo &)> checkLive;
		// Milliseconds since the epoch. Empty = the system clock.
		std::function<qint64()> clock;
	};

	explicit ChatManager(Environment environment);

	// The plugin's manager. install() builds it with the given environment the first time; instance()
	// builds it with an empty one if nothing was installed yet.
	static void install(Environment environment);
	static ChatManager &instance();

	void load();
	void save() const;
	void shutdown();

	// Configured chats in the user's order, paused ones included.
	QList<chat::ChatEntry> chats() const { return chats_; }
	Mode mode() const { return mode_; }
	// The first-run assistant was shown (or skipped); saved in config.json as "wizardShown".
	bool wizardShown() const { return config_.value("wizardShown").toBool(); }
	void setWizardShown();
	// Test messages: the test chat in the dock and the sample messages of the on-screen sources.
	// One switch for everything, saved in config.json as "testMessages".
	bool testMessages() const { return demo_; }
	void setTestMessages(bool on);
	// Scenes from older versions had a per-source "preview": the first one turned on by hand
	// turns the switch on, once (see chat::adoptLegacyPreview). Callable from any thread.
	void adoptLegacyPreview(bool legacyPreview);
	// Changes the chat list: paused chats are disconnected but kept, and sources follow the list's
	// order. Returns the (translated) errors of the invalid entries.
	QStringList configure(const QList<chat::ChatEntry> &chats, Mode mode);

	// Hidden users, commands and blocked words. They apply to messages that arrive from now on.
	chat::FilterRules filters() const { return filters_; }
	void setFilters(const chat::FilterRules &rules) { filters_ = rules; }

	void setStreaming(bool active, const QString &service = {});
	void refresh();
	// Empties the history; undoClear() brings it back until discardUndo() or the next clear.
	void clear();
	bool undoClear();
	void discardUndo();

	// Local moderation from the dock: same path as a platform deletion (dock, overlay and
	// featured drop the message), without touching the platform.
	void hideMessage(const QJsonObject &message);
	// Adds the author to the hidden users filter (saved) and removes their messages from that chat.
	void hideUser(const QJsonObject &message);

	QJsonObject state() const;
	QJsonArray history() const;

	// Mensaje destacado (el que la fuente "Multichat (destacado)" muestra en grande).
	// Vacío si no hay ninguno. featuredAt: cuándo se destacó, en ms desde la época.
	QJsonObject featured() const { return featured_; }
	qint64 featuredAt() const { return featuredAt_; }
	// Destaca el mensaje del historial con ese id. Devuelve false si ya no está.
	bool feature(const QString &id);
	void feature(const QJsonObject &message);
	void clearFeatured();
	// Vuelve a destacar el último mensaje destacado (aunque ya se haya quitado).
	void reshowLast();
	// Auto-hide: each featured source reports its policy (on create/update) and withdraws it
	// when destroyed; the manager hides the message after the shortest enabled duration, so the
	// dock, the hotkeys and every page stay in sync. Callable from any thread.
	void setFeaturedAutoHide(const QString &sourceId, const chat::FeaturedHidePolicy &policy);
	void removeFeaturedAutoHide(const QString &sourceId);

signals:
	void message(const QJsonObject &message);
	void stateChanged(const QJsonObject &state);
	void cleared();
	// undoClear() put the cleared messages back into history().
	void restored();
	// filters() changed outside the settings dialog (a user hidden from the dock).
	void filtersChanged();
	// testMessages() changed (from the dock, the settings or the assistant).
	void testMessagesChanged(bool on);
	// Moderation: messages deleted by the platform (see chat::deleteMessage). `ids` lists the
	// history messages removed; consumers holding more messages use chat::deletionMatches.
	void deleted(const QJsonObject &deletion);
	// Cambió el destacado; vacío = se quitó.
	void featuredChanged(const QJsonObject &message);

private:
	qint64 now() const;
	QString text(const QString &key) const;
	void log(bool warning, const QString &line) const;
	void tick();
	bool wants(const Source &s) const;
	void connectSource(Source &s);
	void disconnectSource(Source &s);
	void reapRetired(bool wait);
	void publishState();
	void receive(QJsonObject object);
	void applyDeletion(QJsonObject deletion);
	void armFeaturedTimer();
	Source *find(const QString &key, quint64 generation);

	Environment env_;
	QList<chat::ChatEntry> chats_;
	QJsonObject config_; // as loaded: keys this version does not know are written back untouched
	QStringList errors_;
	Mode mode_ = Mode::Auto;
	bool demo_ = false; // testMessages(): the "demo:demo" chat is connected
	bool streaming_ = false;
	QString service_;
	qint64 boostUntil_ = 0;
	quint64 nextGeneration_ = 1;
	std::vector<std::unique_ptr<Source>> sources_;
	std::vector<std::shared_ptr<chat::Connector>> retired_;
	QJsonArray history_;
	QJsonArray clearedHistory_; // for undoClear()
	QJsonObject clearedLastFeatured_;
	chat::FilterRules filters_ = chat::FilterRules::defaults();
	QJsonObject featured_;
	QJsonObject lastFeatured_;
	qint64 featuredAt_ = 0;
	QHash<QString, chat::FeaturedHidePolicy> featuredPolicies_; // by featured source id
	QTimer featuredTimer_;
	QJsonObject lastState_;
	QTimer timer_;
	QThreadPool pool_;
	bool shutdown_ = false;
};
