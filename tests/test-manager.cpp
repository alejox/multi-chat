// Characterization tests for ChatManager, without OBS or network: the environment provides a
// temporary config.json, a fake translator, fake connectors and a fake clock. They describe the
// behavior as it is, so refactors of the manager keep it.

#include "../src/chat-manager.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <atomic>
#include <map>
#include <memory>

namespace {

// A connector that only delivers what the test hands it.
class FakeConnector : public chat::Connector {
public:
	using chat::Connector::Connector;
	void send(const QJsonObject &message) { post_(message); }
	void fail(const chat::Text &detail) { report_("error", detail); }
	bool started() const { return started_; }

protected:
	void session() override
	{
		started_ = true;
		while (!wait(20)) {
		}
	}

private:
	std::atomic<bool> started_{false};
};

QJsonObject chatMessage(const QString &id, const QString &author, const QString &text,
			const QString &platform = "twitch")
{
	return QJsonObject{{"id", id},
			   {"platform", platform},
			   {"kind", "chat"},
			   {"author", author},
			   {"authorId", author + "-id"},
			   {"parts", QJsonArray{chat::textPart(text)}}};
}

QStringList ids(const QJsonArray &messages)
{
	QStringList out;
	for (const QJsonValue v : messages)
		out << v.toObject().value("id").toString();
	return out;
}

} // namespace

class ManagerTest : public QObject {
	Q_OBJECT

	std::unique_ptr<QTemporaryDir> dir_;
	std::unique_ptr<ChatManager> manager_;
	std::shared_ptr<std::map<QString, std::shared_ptr<FakeConnector>>> connectors_;
	std::shared_ptr<qint64> clock_;
	std::shared_ptr<QStringList> logs_;

	QString configPath() const { return dir_->filePath("config.json"); }

	void writeConfig(const QJsonObject &config)
	{
		QFile file(configPath());
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(QJsonDocument(config).toJson());
	}

	QJsonObject readConfig() const
	{
		QFile file(configPath());
		if (!file.open(QIODevice::ReadOnly))
			return {};
		return QJsonDocument::fromJson(file.readAll()).object();
	}

	ChatManager &make()
	{
		ChatManager::Environment env;
		env.store = ConfigStore(configPath());
		env.translate = [](const QString &key) {
			if (key == "Message.Test")
				return QString("Translated %1");
			if (key.startsWith("Entry.Error."))
				return key + " %1";
			return key; // missing keys come back as is, like obs_module_text
		};
		auto logs = logs_;
		env.log = [logs](bool warning, const QString &line) { logs->append((warning ? "W " : "I ") + line); };
		auto connectors = connectors_;
		env.createConnector = [connectors](const chat::SourceInfo &source, chat::Connector::PostFn post,
						   chat::Connector::ReportFn report) {
			auto c = std::make_shared<FakeConnector>(source, std::move(post), std::move(report));
			(*connectors)[source.key] = c;
			return std::shared_ptr<chat::Connector>(c);
		};
		env.checkLive = [](const chat::SourceInfo &) { return chat::LiveResult{}; };
		auto clock = clock_;
		env.clock = [clock] { return *clock; };
		manager_ = std::make_unique<ChatManager>(std::move(env));
		return *manager_;
	}

	// Two Twitch chats, connected whether or not they are live.
	ChatManager &makeConnected()
	{
		ChatManager &m = make();
		m.configure({{"twitch:a", true}, {"twitch:b", true}}, ChatManager::Mode::Always);
		return m;
	}

	void deliver(const QString &source, const QJsonObject &message)
	{
		auto it = connectors_->find(source);
		QVERIFY2(it != connectors_->end(), qPrintable(source));
		it->second->send(message);
		QCoreApplication::processEvents();
	}

private slots:
	void init()
	{
		dir_ = std::make_unique<QTemporaryDir>();
		QVERIFY(dir_->isValid());
		connectors_ = std::make_shared<std::map<QString, std::shared_ptr<FakeConnector>>>();
		clock_ = std::make_shared<qint64>(1000000);
		logs_ = std::make_shared<QStringList>();
	}

	void cleanup()
	{
		if (manager_)
			manager_->shutdown();
		manager_.reset();
		connectors_.reset();
	}

	void receivesInOrderAndTagsTheSource()
	{
		ChatManager &m = makeConnected();
		QSignalSpy spy(&m, &ChatManager::message);
		deliver("twitch:a", chatMessage("1", "ana", "hola"));
		deliver("twitch:b", chatMessage("2", "bob", "buenas"));
		QCOMPARE(spy.count(), 2);
		QCOMPARE(ids(m.history()), QStringList({"1", "2"}));
		QCOMPARE(m.history().at(0).toObject().value("source").toString(), QString("twitch:a"));
		QCOMPARE(m.history().at(1).toObject().value("source").toString(), QString("twitch:b"));
	}

	void localizesSystemLinesWithTheTranslator()
	{
		ChatManager &m = makeConnected();
		QJsonObject msg = chatMessage("1", "ana", "");
		msg.insert("systemKey", "Message.Test");
		msg.insert("systemArgs", QJsonArray{"x"});
		deliver("twitch:a", msg);
		QCOMPARE(m.history().at(0).toObject().value("system").toString(), QString("Translated x"));
	}

	void keepsTheLastMessagesUpToTheLimit()
	{
		ChatManager &m = makeConnected();
		const int limit = chat::kHistoryLimit;
		for (int i = 0; i < limit + 5; ++i)
			deliver("twitch:a", chatMessage(QString::number(i), "ana", "m"));
		QCOMPARE(m.history().size(), limit);
		QCOMPARE(m.history().first().toObject().value("id").toString(), QString("5"));
	}

	void filtersCommandsHiddenUsersAndBlockedWords()
	{
		ChatManager &m = makeConnected();
		chat::FilterRules rules = chat::FilterRules::defaults();
		rules.hiddenUsers = {"Nightbot"};
		rules.blockedWords = {"spam"};
		m.setFilters(rules);
		QSignalSpy spy(&m, &ChatManager::message);
		deliver("twitch:a", chatMessage("1", "ana", "!comando"));
		deliver("twitch:a", chatMessage("2", "nightbot", "hola"));
		deliver("twitch:a", chatMessage("3", "ana", "esto es SPAM"));
		deliver("twitch:a", chatMessage("4", "ana", "hola"));
		QCOMPARE(spy.count(), 1);
		QCOMPARE(ids(m.history()), QStringList({"4"}));
	}

	void deletesByIdOnlyInItsSource()
	{
		ChatManager &m = makeConnected();
		deliver("twitch:a", chatMessage("1", "ana", "a"));
		deliver("twitch:b", chatMessage("1", "ana", "b"));
		QSignalSpy spy(&m, &ChatManager::deleted);
		deliver("twitch:a", chat::deleteMessage("twitch", "1"));
		QCOMPARE(m.history().size(), 1);
		QCOMPARE(m.history().at(0).toObject().value("source").toString(), QString("twitch:b"));
		QCOMPARE(spy.count(), 1);
		const QJsonObject deletion = spy.at(0).at(0).toJsonObject();
		QCOMPARE(deletion.value("ids").toArray(), QJsonArray{"1"});
		QCOMPARE(deletion.value("source").toString(), QString("twitch:a"));
	}

	void deletesByAuthorAndClearsAChannel()
	{
		ChatManager &m = makeConnected();
		deliver("twitch:a", chatMessage("1", "ana", "a"));
		deliver("twitch:a", chatMessage("2", "bob", "b"));
		deliver("twitch:a", chatMessage("3", "ana", "c"));
		deliver("twitch:b", chatMessage("4", "ana", "d"));
		deliver("twitch:a", chat::deleteAuthor("twitch", "ana-id", "ana"));
		QCOMPARE(ids(m.history()), QStringList({"2", "4"}));
		deliver("twitch:b", chat::clearChannel("twitch"));
		QCOMPARE(ids(m.history()), QStringList({"2"}));
	}

	void hidesMessagesAndUsersLocally()
	{
		ChatManager &m = makeConnected();
		deliver("twitch:a", chatMessage("1", "ana", "a"));
		deliver("twitch:a", chatMessage("2", "bob", "b"));
		deliver("twitch:a", chatMessage("3", "ana", "c"));
		QSignalSpy filters(&m, &ChatManager::filtersChanged);
		m.hideMessage(m.history().at(1).toObject());
		QCOMPARE(ids(m.history()), QStringList({"1", "3"}));
		QCOMPARE(filters.count(), 0);

		m.hideUser(m.history().at(0).toObject());
		QCOMPARE(m.history().size(), 0);
		QCOMPARE(filters.count(), 1);
		const int defaults = int(chat::FilterRules::defaults().hiddenUsers.size());
		QCOMPARE(m.filters().hiddenUsers.size(), defaults + 1); // appended after the default bots
		QCOMPARE(m.filters().hiddenUsers.last(), QString("ana"));
		QVERIFY(readConfig().value("filters").toObject().value("hiddenUsers").toArray().contains("ana"));
		// Hiding the same user again does not add it twice.
		deliver("twitch:b", chatMessage("4", "ANA", "d"));
		m.hideUser(chatMessage("5", "Ana", "e"));
		QCOMPARE(filters.count(), 1);
		QCOMPARE(m.filters().hiddenUsers.size(), defaults + 1);
	}

	void featuresFromHistoryAndReshows()
	{
		ChatManager &m = makeConnected();
		deliver("twitch:a", chatMessage("1", "ana", "hola"));
		QSignalSpy spy(&m, &ChatManager::featuredChanged);
		QVERIFY(!m.feature(QString("missing")));
		QVERIFY(!m.feature(QString()));
		QVERIFY(m.feature(QString("1")));
		QCOMPARE(m.featured().value("id").toString(), QString("1"));
		QCOMPARE(m.featuredAt(), *clock_);
		QCOMPARE(spy.count(), 1);

		*clock_ += 500;
		m.clearFeatured();
		QVERIFY(m.featured().isEmpty());
		QCOMPARE(m.featuredAt(), *clock_);
		QCOMPARE(spy.count(), 2);
		m.clearFeatured(); // nothing on screen: no signal
		QCOMPARE(spy.count(), 2);

		m.reshowLast();
		QCOMPARE(m.featured().value("id").toString(), QString("1"));
		m.feature(QJsonObject()); // an empty message removes it
		QVERIFY(m.featured().isEmpty());
	}

	void deletingTheFeaturedMessageRemovesItAndItsReshow()
	{
		ChatManager &m = makeConnected();
		deliver("twitch:a", chatMessage("1", "ana", "hola"));
		QVERIFY(m.feature(QString("1")));
		deliver("twitch:a", chat::deleteMessage("twitch", "1"));
		QVERIFY(m.featured().isEmpty());
		m.reshowLast();
		QVERIFY(m.featured().isEmpty());
	}

	void deletingInAnotherSourceKeepsTheFeatured()
	{
		ChatManager &m = makeConnected();
		deliver("twitch:a", chatMessage("1", "ana", "hola"));
		QVERIFY(m.feature(QString("1")));
		deliver("twitch:b", chat::deleteMessage("twitch", "1"));
		QCOMPARE(m.featured().value("id").toString(), QString("1"));
	}

	void featuredExpiresByTheShortestPolicy()
	{
		ChatManager &m = makeConnected();
		deliver("twitch:a", chatMessage("1", "ana", "hola"));
		chat::FeaturedHidePolicy slow{true, 20, 0, 30};
		chat::FeaturedHidePolicy fast{true, 8, 0, 30};
		chat::FeaturedHidePolicy never{false, 8, 0, 30};
		m.setFeaturedAutoHide("slow", slow);
		m.setFeaturedAutoHide("never", never);
		QCoreApplication::processEvents();
		QVERIFY(m.feature(QString("1")));

		// Time passes (fake clock); a policy update re-arms from featuredAt, not from now.
		*clock_ += 10000;
		m.setFeaturedAutoHide("fast", fast);
		QCoreApplication::processEvents();
		QVERIFY(m.featured().isEmpty());
	}

	void featuredStaysWhenNoPolicyHides()
	{
		ChatManager &m = makeConnected();
		deliver("twitch:a", chatMessage("1", "ana", "hola"));
		m.setFeaturedAutoHide("never", chat::FeaturedHidePolicy{false, 8, 0, 30});
		QCoreApplication::processEvents();
		QVERIFY(m.feature(QString("1")));
		*clock_ += 3600000;
		m.setFeaturedAutoHide("never", chat::FeaturedHidePolicy{false, 8, 0, 30});
		QCoreApplication::processEvents();
		QVERIFY(!m.featured().isEmpty());

		// Removing the last hiding policy keeps it too; a zero-length one hides at once.
		m.removeFeaturedAutoHide("never");
		QCoreApplication::processEvents();
		QVERIFY(!m.featured().isEmpty());
		m.setFeaturedAutoHide("zero", chat::FeaturedHidePolicy{true, 0, 0, 0});
		QCoreApplication::processEvents();
		QVERIFY(m.featured().isEmpty());
	}

	void clearAndUndo()
	{
		ChatManager &m = makeConnected();
		deliver("twitch:a", chatMessage("1", "ana", "a"));
		deliver("twitch:a", chatMessage("2", "bob", "b"));
		QVERIFY(m.feature(QString("2")));
		QSignalSpy cleared(&m, &ChatManager::cleared);
		QSignalSpy restored(&m, &ChatManager::restored);

		m.clear();
		QCOMPARE(cleared.count(), 1);
		QCOMPARE(m.history().size(), 0);
		QVERIFY(m.featured().isEmpty());
		m.reshowLast(); // the last featured goes with the clear
		QVERIFY(m.featured().isEmpty());

		deliver("twitch:a", chatMessage("3", "ana", "c"));
		QVERIFY(m.undoClear());
		QCOMPARE(restored.count(), 1);
		QCOMPARE(ids(m.history()), QStringList({"1", "2", "3"}));
		m.reshowLast(); // ...and comes back with the undo
		QCOMPARE(m.featured().value("id").toString(), QString("2"));
		QVERIFY(!m.undoClear());
	}

	// Clearing twice before undoing keeps both clears: undo brings back everything, in order,
	// and the last featured message from before the first clear.
	void clearTwiceThenUndoBringsBothBack()
	{
		ChatManager &m = makeConnected();
		deliver("twitch:a", chatMessage("1", "ana", "a"));
		deliver("twitch:a", chatMessage("2", "bob", "b"));
		QVERIFY(m.feature(QString("2")));
		m.clear();
		deliver("twitch:a", chatMessage("3", "ana", "c"));
		m.clear();
		deliver("twitch:a", chatMessage("4", "bob", "d"));
		QVERIFY(m.undoClear());
		QCOMPARE(ids(m.history()), QStringList({"1", "2", "3", "4"}));
		m.reshowLast();
		QCOMPARE(m.featured().value("id").toString(), QString("2"));
		QVERIFY(!m.undoClear());
	}

	void undoSkipsModeratedMessages()
	{
		ChatManager &m = makeConnected();
		deliver("twitch:a", chatMessage("1", "ana", "a"));
		deliver("twitch:a", chatMessage("2", "bob", "b"));
		QVERIFY(m.feature(QString("1")));
		m.clear();
		deliver("twitch:a", chat::deleteMessage("twitch", "1"));
		QVERIFY(m.undoClear());
		QCOMPARE(ids(m.history()), QStringList({"2"}));
		m.reshowLast();
		QVERIFY(m.featured().isEmpty());
	}

	void discardUndoAndEmptyUndo()
	{
		ChatManager &m = makeConnected();
		QVERIFY(!m.undoClear());
		deliver("twitch:a", chatMessage("1", "ana", "a"));
		m.clear();
		m.discardUndo();
		QVERIFY(!m.undoClear());
		QCOMPARE(m.history().size(), 0);
	}

	void undoKeepsTheHistoryLimit()
	{
		ChatManager &m = makeConnected();
		const int limit = chat::kHistoryLimit;
		for (int i = 0; i < limit - 50; ++i)
			deliver("twitch:a", chatMessage(QString::number(i), "ana", "m"));
		m.clear();
		for (int i = limit - 50; i < limit + 50; ++i)
			deliver("twitch:a", chatMessage(QString::number(i), "ana", "m"));
		QVERIFY(m.undoClear());
		QCOMPARE(m.history().size(), limit);
		QCOMPARE(m.history().first().toObject().value("id").toString(), QString("50"));
		QCOMPARE(m.history().last().toObject().value("id").toString(), QString::number(limit + 49));
	}

	void testMessagesAddTheDemoChat()
	{
		ChatManager &m = make();
		m.load();
		QVERIFY(!m.testMessages());
		QSignalSpy spy(&m, &ChatManager::testMessagesChanged);
		m.setTestMessages(true);
		QVERIFY(m.testMessages());
		QCOMPARE(spy.count(), 1);
		QCOMPARE(readConfig().value("testMessages").toBool(), true);
		const QJsonArray sources = m.state().value("sources").toArray();
		QCOMPARE(sources.size(), 1);
		QCOMPARE(sources.at(0).toObject().value("platform").toString(), QString("demo"));
		QCOMPARE(sources.at(0).toObject().value("status").toString(), QString("connecting"));

		m.setTestMessages(true); // already on and saved: nothing happens
		QCOMPARE(spy.count(), 1);
		m.setTestMessages(false);
		QCOMPARE(spy.count(), 2);
		QCOMPARE(readConfig().value("testMessages").toBool(true), false);
		QCOMPARE(m.state().value("sources").toArray().size(), 0);
	}

	void legacyPreviewTurnsTestMessagesOnOnce()
	{
		ChatManager &m = make();
		m.load();
		QSignalSpy spy(&m, &ChatManager::testMessagesChanged);
		m.adoptLegacyPreview(false);
		QCoreApplication::processEvents();
		QCOMPARE(spy.count(), 0);
		m.adoptLegacyPreview(true);
		QCoreApplication::processEvents();
		QCOMPARE(spy.count(), 1);
		QVERIFY(m.testMessages());
		QVERIFY(logs_->contains("I test messages turned on: a source had its sample messages enabled"));

		// Once decided (even off), a legacy preview no longer changes it.
		m.setTestMessages(false);
		m.adoptLegacyPreview(true);
		QCoreApplication::processEvents();
		QVERIFY(!m.testMessages());
	}

	void loadsAndSavesConfigKeepingUnknownKeys()
	{
		writeConfig(QJsonObject{{"chats", QJsonArray{"twitch:a", "kick:b"}},
					{"disabled", QJsonArray{"kick:b"}},
					{"mode", "always"},
					{"filters", QJsonObject{{"hiddenUsers", QJsonArray{"bot"}}, {"hideCommands", false}}},
					{"futureKey", QJsonObject{{"x", 1}}}});
		ChatManager &m = make();
		m.load();
		QCOMPARE(m.mode(), ChatManager::Mode::Always);
		QCOMPARE(m.chats().size(), 2);
		QCOMPARE(m.chats().at(1).enabled, false);
		QCOMPARE(m.filters().hiddenUsers, QStringList({"bot"}));
		QCOMPARE(m.filters().hideCommands, false);
		QVERIFY(!m.wizardShown());
		QCOMPARE(m.state().value("sources").toArray().size(), 1); // the paused chat is not connected

		m.setWizardShown();
		const QJsonObject saved = readConfig();
		QCOMPARE(saved.value("futureKey").toObject().value("x").toInt(), 1);
		QCOMPARE(saved.value("wizardShown").toBool(), true);
		QCOMPARE(saved.value("mode").toString(), QString("always"));
		QCOMPARE(saved.value("disabled").toArray(), QJsonArray{"kick:b"});
		QCOMPARE(saved.value("filters").toObject().value("hiddenUsers").toArray(), QJsonArray{"bot"});
	}

	void configureReportsTranslatedErrors()
	{
		ChatManager &m = make();
		const QStringList errors = m.configure({{"nope:x", true}, {" ", true}}, ChatManager::Mode::Auto);
		QCOMPARE(errors.size(), 1);
		QVERIFY(errors.at(0).startsWith("Entry.Error.")); // the fake translator returns the key
		QCOMPARE(m.chats().size(), 1);                     // blank entries are dropped
		QCOMPARE(m.state().value("errors").toArray().size(), 1);
	}

	void autoModeWaitsForLiveOrStreaming()
	{
		ChatManager &m = make();
		m.configure({{"twitch:a", true}}, ChatManager::Mode::Auto);
		QCOMPARE(connectors_->size(), size_t(0));
		m.setStreaming(true, "Restream.io");
		QCOMPARE(connectors_->size(), size_t(1));
		const QJsonObject state = m.state();
		QCOMPARE(state.value("streaming").toBool(), true);
		QCOMPARE(state.value("multistream").toBool(), true); // a multistream service
		QCOMPARE(state.value("mode").toString(), QString("auto"));
		m.setStreaming(false);
		QCOMPARE(m.state().value("sources").toArray().at(0).toObject().value("status").toString(),
			 QString("idle"));
	}

	void reportedErrorsAreTranslatedAndLogged()
	{
		ChatManager &m = makeConnected();
		auto connector = connectors_->at("twitch:a");
		QTRY_VERIFY(connector->started());
		QCoreApplication::processEvents(); // the connector's own "connecting" report
		connector->fail(chat::Text{"Message.Test", {"x"}});
		QCoreApplication::processEvents();
		const QJsonObject source = m.state().value("sources").toArray().at(0).toObject();
		QCOMPARE(source.value("status").toString(), QString("error"));
		QCOMPARE(source.value("detail").toString(), QString("Translated x"));
		QCOMPARE(logs_->size(), 1);
		QVERIFY(logs_->at(0).startsWith("I [twitch:a] "));
	}

	void historyRemovesNewestFirstAndPurgesTheUndoCopy()
	{
		ChatHistory h;
		QJsonObject a = chatMessage("1", "ana", "a"), b = chatMessage("2", "ana", "b");
		a.insert("source", "twitch:a");
		b.insert("source", "twitch:a");
		h.append(a);
		h.append(b);
		QJsonObject del = chat::deleteAuthor("twitch", "ana-id", "ana");
		del.insert("source", "twitch:a");
		QCOMPARE(h.remove(del), QJsonArray({"2", "1"}));
		h.append(a);
		h.clear();
		QVERIFY(h.canRestore());
		h.remove(del);
		QVERIFY(!h.canRestore());
	}

	void featuredRemainingTimeCountsFromWhenItWasShown()
	{
		FeaturedState f;
		QVERIFY(!f.remainingMs(0));
		f.show(chatMessage("1", "ana", "hola"), 1000);
		QVERIFY(!f.remainingMs(1000)); // no policy
		f.setPolicy("x", chat::FeaturedHidePolicy{true, 8, 0, 30});
		QCOMPARE(*f.remainingMs(3000), qint64(6000));
		QVERIFY(f.hide(4000));
		QVERIFY(!f.hide(5000));
		QCOMPARE(f.changedAt(), qint64(4000));
		QCOMPARE(f.last().value("id").toString(), QString("1"));
	}

	void configStoreWritesTheVersion()
	{
		ConfigStore store(configPath());
		QVERIFY(store.save(QJsonObject{{"mode", "auto"}}));
		QCOMPARE(readConfig().value("version").toInt(), ConfigStore::kVersion);
		QCOMPARE(ConfigStore::kVersion, 1);
		QCOMPARE(dir_->path() + "/config.json", configPath());
		QCOMPARE(QDir(dir_->path()).entryList(QDir::Files), QStringList({"config.json"})); // no temp files
	}

	void configStoreReadsAMissingVersionAsOne()
	{
		writeConfig(QJsonObject{{"mode", "always"}});
		const QJsonObject loaded = ConfigStore(configPath()).load();
		QCOMPARE(loaded.value("version").toInt(), 1);
		QCOMPARE(loaded.value("mode").toString(), QString("always"));
		QVERIFY(ConfigStore(dir_->filePath("none.json")).load().isEmpty());
	}

	void jsonFilesAreReplacedWhole()
	{
		const QString path = dir_->filePath("sub/dock.json");
		QVERIFY(writeJsonObject(path, QJsonObject{{"a", 1}, {"b", 2}}));
		QVERIFY(writeJsonObject(path, QJsonObject{{"a", 3}}));
		QCOMPARE(readJsonObject(path), QJsonObject({{"a", 3}}));
		QVERIFY(!writeJsonObject(QString(), QJsonObject{{"a", 1}}));
	}

	void saveFailureIsLogged()
	{
		ChatManager::Environment env;
		env.store = ConfigStore(dir_->filePath("missing-dir/config.json/x"));
		QFile blocker(dir_->filePath("missing-dir"));
		QVERIFY(blocker.open(QIODevice::WriteOnly)); // a file where the folder should be
		blocker.close();
		auto logs = logs_;
		env.log = [logs](bool warning, const QString &line) { logs->append((warning ? "W " : "I ") + line); };
		env.checkLive = [](const chat::SourceInfo &) { return chat::LiveResult{}; };
		manager_ = std::make_unique<ChatManager>(std::move(env));
		manager_->setWizardShown();
		QCOMPARE(logs_->size(), 1);
		QVERIFY(logs_->at(0).startsWith("W could not save "));
	}
};

QTEST_GUILESS_MAIN(ManagerTest)
#include "test-manager.moc"
