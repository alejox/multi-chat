#include "connector.hpp"

#include <algorithm>
#include <chrono>

namespace chat {

Connector::Connector(SourceInfo source, PostFn post, ReportFn report)
	: source_(std::move(source)),
	  post_(std::move(post)),
	  report_(std::move(report))
{
}

Connector::~Connector()
{
	// El dueño siempre llama a join(); esto es solo por seguridad.
	if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
		thread_.join();
	else if (thread_.joinable())
		thread_.detach();
}

void Connector::start()
{
	auto self = shared_from_this();
	thread_ = std::thread([self] { self->run(); });
}

void Connector::stop()
{
	stopping_ = true;
	std::shared_ptr<net::WebSocket> ws;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		ws = ws_;
	}
	cv_.notify_all();
	if (ws)
		ws->close();
}

void Connector::join()
{
	if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
		thread_.join();
}

bool Connector::wait(int ms)
{
	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return stopping_.load(); });
	return stopping_;
}

void Connector::setSocket(std::shared_ptr<net::WebSocket> ws)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		ws_ = ws;
	}
	if (ws && stopping_)
		ws->close();
}

std::shared_ptr<net::WebSocket> Connector::openSocket(const std::string &url, const net::Headers &headers)
{
	std::string error;
	auto ws = net::connectWebSocket(url, headers, error);
	if (!ws)
		throw ChatError("no se pudo conectar: " + error);
	setSocket(ws);
	return ws;
}

void Connector::run()
{
	int backoff = 2000;
	while (!stopped()) {
		auto started = std::chrono::steady_clock::now();
		try {
			report_("connecting", QString());
			session();
		} catch (const std::exception &e) {
			if (stopped())
				break;
			report_("error", QString::fromUtf8(e.what()).left(200));
		}
		std::shared_ptr<net::WebSocket> ws;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			ws.swap(ws_);
		}
		if (ws)
			ws->close();
		if (stopped())
			break;
		if (std::chrono::steady_clock::now() - started > std::chrono::seconds(60))
			backoff = 2000;
		if (wait(backoff))
			break;
		backoff = std::min(backoff * 2, 60000);
	}
	done_ = true;
}

} // namespace chat
