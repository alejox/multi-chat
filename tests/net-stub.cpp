// Red de mentira para compilar y probar los parsers fuera de Windows/macOS.

#include "../src/net/net.hpp"

namespace net {

const char *const kUserAgent = "multichat-tests";

Response request(const std::string &, const std::string &, const Headers &, const std::string &, int)
{
	Response r;
	r.error = "sin red en las pruebas";
	return r;
}

std::shared_ptr<WebSocket> connectWebSocket(const std::string &, const Headers &, std::string &error)
{
	error = "sin red en las pruebas";
	return nullptr;
}

} // namespace net
