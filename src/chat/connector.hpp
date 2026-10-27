#pragma once

// Conector de chat: un hilo que mantiene la conexión con una plataforma y reconecta solo.

#include "../net/net.hpp"

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace chat {

// Un chat configurado por el usuario ("twitch:micanal").
struct SourceInfo {
	QString key;             // twitch:micanal (en minúsculas)
	QString platform;        // twitch | youtube | kick | demo
	QString channel;         // micanal · @micanal · id del vídeo
	QString extra;           // Kick: id del chatroom · YouTube: id del directo
	bool videoFixed = false; // YouTube: el usuario indicó un directo concreto
};

// Lanza esto dentro de session() para reportar un error al usuario.
struct ChatError : std::runtime_error {
	using std::runtime_error::runtime_error;
	explicit ChatError(const QString &message) : std::runtime_error(message.toStdString()) {}
};

class Connector : public std::enable_shared_from_this<Connector> {
public:
	using PostFn = std::function<void(QJsonObject)>;
	using ReportFn = std::function<void(const QString &status, const QString &detail)>;

	Connector(SourceInfo source, PostFn post, ReportFn report);
	virtual ~Connector();

	void start();
	// No bloquea: pide parar y corta la conexión. Luego hay que llamar a join().
	void stop();
	bool finished() const { return done_; }
	void join();

protected:
	virtual void session() = 0;

	bool stopped() const { return stopping_; }
	// Espera interrumpible. Devuelve true si se pidió parar.
	bool wait(int ms);
	// Registra el WebSocket activo para que stop() pueda cortarlo.
	void setSocket(std::shared_ptr<net::WebSocket> ws);
	std::shared_ptr<net::WebSocket> openSocket(const std::string &url, const net::Headers &headers = {});

	SourceInfo source_;
	PostFn post_;
	ReportFn report_;

private:
	void run();

	std::thread thread_;
	std::atomic<bool> stopping_{false};
	std::atomic<bool> done_{false};
	std::mutex mutex_;
	std::condition_variable cv_;
	std::shared_ptr<net::WebSocket> ws_;
};

} // namespace chat
