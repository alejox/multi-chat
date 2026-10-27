#pragma once

// Red con las APIs del sistema: WinHTTP en Windows y NSURLSession en macOS.
// OBS no instala los plugins TLS de Qt, así que QtNetwork no sirve para HTTPS dentro de OBS.
// Todas las funciones son bloqueantes: se llaman solo desde hilos de trabajo.

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace net {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Response {
	int status = 0; // 0 = no hubo respuesta (ver error)
	std::string body;
	std::string error;
	bool ok() const { return status >= 200 && status < 300; }
};

Response request(const std::string &method, const std::string &url, const Headers &headers = {},
		 const std::string &body = {}, int timeoutMs = 10000);

inline Response get(const std::string &url, const Headers &headers = {}, int timeoutMs = 10000)
{
	return request("GET", url, headers, {}, timeoutMs);
}

inline Response postJson(const std::string &url, const std::string &json, Headers headers = {}, int timeoutMs = 10000)
{
	headers.emplace_back("Content-Type", "application/json");
	return request("POST", url, headers, json, timeoutMs);
}

class WebSocket {
public:
	enum class Result { Message, Closed };

	virtual ~WebSocket() = default;
	// Bloquea hasta recibir un mensaje de texto completo o hasta que se cierre la conexión.
	virtual Result receive(std::string &message) = 0;
	virtual bool send(const std::string &text) = 0;
	// Se puede llamar desde otro hilo: desbloquea receive().
	virtual void close() = 0;
};

std::shared_ptr<WebSocket> connectWebSocket(const std::string &url, const Headers &headers, std::string &error);

extern const char *const kUserAgent;

} // namespace net
