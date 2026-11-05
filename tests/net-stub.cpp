// Red de mentira para compilar y probar los parsers fuera de Windows/macOS. Las pruebas de los
// conectores pueden darle respuestas y sockets programados.

#include "../src/net/net.hpp"

#include <functional>
#include <mutex>

namespace netstub {

using RequestHandler =
	std::function<net::Response(const std::string &method, const std::string &url, const std::string &body)>;
using SocketFactory = std::function<std::shared_ptr<net::WebSocket>(const std::string &url)>;

namespace {
std::mutex mutex;
RequestHandler requestHandler;
SocketFactory socketFactory;
} // namespace

void set(RequestHandler handler, SocketFactory factory)
{
	std::lock_guard<std::mutex> lock(mutex);
	requestHandler = std::move(handler);
	socketFactory = std::move(factory);
}

} // namespace netstub

namespace net {

const char *const kUserAgent = "multichat-tests";

Response request(const std::string &method, const std::string &url, const Headers &, const std::string &body, int)
{
	netstub::RequestHandler handler;
	{
		std::lock_guard<std::mutex> lock(netstub::mutex);
		handler = netstub::requestHandler;
	}
	if (handler)
		return handler(method, url, body);
	Response r;
	r.error = "sin red en las pruebas";
	return r;
}

std::shared_ptr<WebSocket> connectWebSocket(const std::string &url, const Headers &, std::string &error)
{
	netstub::SocketFactory factory;
	{
		std::lock_guard<std::mutex> lock(netstub::mutex);
		factory = netstub::socketFactory;
	}
	if (factory)
		return factory(url);
	error = "sin red en las pruebas";
	return nullptr;
}

} // namespace net
