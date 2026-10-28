#pragma once

// Gestiona los chats configurados: detecta qué plataformas están en vivo (multistream),
// conecta y desconecta los chats y reparte los mensajes al dock y al overlay.
// Vive en el hilo principal de Qt; los conectores y las consultas corren en hilos aparte.

#include "chat/platforms.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>

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

	static ChatManager &instance();

	void load();
	void save() const;
	void shutdown();

	QStringList entries() const { return entries_; }
	Mode mode() const { return mode_; }
	bool demo() const { return demo_; }
	// Cambia la lista de chats. Devuelve los errores de las entradas no válidas.
	QStringList configure(const QStringList &entries, Mode mode, bool demo);

	void setStreaming(bool active, const QString &service = {});
	void refresh();
	void clear();

	QJsonObject state() const;
	QJsonArray history() const;

signals:
	void message(const QJsonObject &message);
	void stateChanged(const QJsonObject &state);
	void cleared();

private:
	ChatManager();
	void tick();
	bool wants(const Source &s) const;
	void connectSource(Source &s);
	void disconnectSource(Source &s);
	void reapRetired(bool wait);
	void publishState();
	Source *find(const QString &key, quint64 generation);

	QStringList entries_;
	QStringList errors_;
	Mode mode_ = Mode::Auto;
	bool demo_ = false;
	bool streaming_ = false;
	QString service_;
	qint64 boostUntil_ = 0;
	quint64 nextGeneration_ = 1;
	std::vector<std::unique_ptr<Source>> sources_;
	std::vector<std::shared_ptr<chat::Connector>> retired_;
	QJsonArray history_;
	QJsonObject lastState_;
	QTimer timer_;
	QThreadPool pool_;
	bool shutdown_ = false;
};
