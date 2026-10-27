// Red en Windows con WinHTTP (HTTPS y WebSocket del sistema, Windows 8+).

#include "net.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <mutex>

namespace net {

const char *const kUserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
			       "(KHTML, like Gecko) Chrome/130.0 Safari/537.36";

namespace {

constexpr size_t kMaxBody = 16 * 1024 * 1024;

std::wstring widen(const std::string &s)
{
	if (s.empty())
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring out(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
	return out;
}

std::string lastError(const char *what)
{
	return std::string(what) + " (WinHTTP " + std::to_string(GetLastError()) + ")";
}

struct Handle {
	HINTERNET h = nullptr;
	Handle() = default;
	explicit Handle(HINTERNET handle) : h(handle) {}
	Handle(const Handle &) = delete;
	Handle &operator=(const Handle &) = delete;
	~Handle()
	{
		if (h)
			WinHttpCloseHandle(h);
	}
	operator HINTERNET() const { return h; }
	HINTERNET release()
	{
		HINTERNET out = h;
		h = nullptr;
		return out;
	}
};

// Una sesión para todo el plugin (WinHTTP permite usarla desde varios hilos).
HINTERNET session()
{
	static HINTERNET s = [] {
		const std::wstring ua = widen(kUserAgent);
		HINTERNET h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
					  WINHTTP_NO_PROXY_BYPASS, 0);
		if (!h) // Windows antiguo sin proxy automático
			h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
					WINHTTP_NO_PROXY_BYPASS, 0);
		if (h) {
			DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
			WinHttpSetOption(h, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
			DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
			if (!WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
				protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
				WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
			}
		}
		return h;
	}();
	return s;
}

struct Url {
	std::wstring host, path;
	INTERNET_PORT port = 0;
	bool secure = false;
};

bool parseUrl(std::string url, Url &out)
{
	// WinHttpCrackUrl no conoce ws:// ni wss://.
	if (url.rfind("wss://", 0) == 0)
		url = "https://" + url.substr(6);
	else if (url.rfind("ws://", 0) == 0)
		url = "http://" + url.substr(5);
	const std::wstring w = widen(url);
	URL_COMPONENTS uc = {};
	uc.dwStructSize = sizeof(uc);
	uc.dwSchemeLength = (DWORD)-1;
	uc.dwHostNameLength = (DWORD)-1;
	uc.dwUrlPathLength = (DWORD)-1;
	uc.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(w.c_str(), (DWORD)w.size(), 0, &uc))
		return false;
	out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
	out.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
	if (uc.lpszExtraInfo)
		out.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
	if (out.path.empty())
		out.path = L"/";
	out.port = uc.nPort;
	out.secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
	return true;
}

std::wstring headerBlock(const Headers &headers)
{
	std::string block;
	for (const auto &[name, value] : headers)
		block += name + ": " + value + "\r\n";
	return widen(block);
}

// Abre la conexión y envía la petición. Devuelve el handle de la petición con la respuesta lista.
HINTERNET openRequest(const char *method, const std::string &url, const Headers &headers, const std::string &body,
		      int timeoutMs, bool websocket, HINTERNET &connection, std::string &error)
{
	Url u;
	if (!session()) {
		error = lastError("WinHttpOpen");
		return nullptr;
	}
	if (!parseUrl(url, u)) {
		error = "URL no válida: " + url;
		return nullptr;
	}
	Handle conn(WinHttpConnect(session(), u.host.c_str(), u.port, 0));
	if (!conn) {
		error = lastError("WinHttpConnect");
		return nullptr;
	}
	const std::wstring verb = widen(method);
	Handle req(WinHttpOpenRequest(conn, verb.c_str(), u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
				      WINHTTP_DEFAULT_ACCEPT_TYPES, u.secure ? WINHTTP_FLAG_SECURE : 0));
	if (!req) {
		error = lastError("WinHttpOpenRequest");
		return nullptr;
	}
	// En un WebSocket la espera de mensajes no tiene límite (Twitch hace PING cada 5 min).
	WinHttpSetTimeouts(req, timeoutMs, timeoutMs, timeoutMs, websocket ? 0 : timeoutMs);
	if (websocket && !WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
		error = lastError("WebSocket");
		return nullptr;
	}
	const std::wstring hdrs = headerBlock(headers);
	if (!WinHttpSendRequest(req, hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(),
				hdrs.empty() ? 0 : (DWORD)-1L,
				body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(),
				(DWORD)body.size(), 0) ||
	    !WinHttpReceiveResponse(req, nullptr)) {
		error = lastError("sin respuesta");
		return nullptr;
	}
	connection = conn.release();
	return req.release();
}

int statusCode(HINTERNET req)
{
	DWORD status = 0, size = sizeof(status);
	WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
			    &status, &size, WINHTTP_NO_HEADER_INDEX);
	return (int)status;
}

class WinWebSocket : public WebSocket {
public:
	WinWebSocket(HINTERNET connection, HINTERNET ws) : connection_(connection), ws_(ws) {}
	~WinWebSocket() override
	{
		close();
		WinHttpCloseHandle(connection_);
	}

	Result receive(std::string &message) override
	{
		message.clear();
		char buffer[16384];
		for (;;) {
			HINTERNET ws = ws_.load();
			if (!ws)
				return Result::Closed;
			DWORD read = 0;
			WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
			if (WinHttpWebSocketReceive(ws, buffer, sizeof(buffer), &read, &type) != NO_ERROR)
				return Result::Closed;
			if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
				return Result::Closed;
			message.append(buffer, read);
			if (message.size() > kMaxBody)
				return Result::Closed;
			if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
			    type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
				return Result::Message;
			// *_FRAGMENT_BUFFER_TYPE: sigue leyendo el resto del mensaje.
		}
	}

	bool send(const std::string &text) override
	{
		std::lock_guard<std::mutex> lock(sendMutex_);
		HINTERNET ws = ws_.load();
		return ws && WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)text.data(),
						  (DWORD)text.size()) == NO_ERROR;
	}

	void close() override
	{
		// Cerrar el handle cancela el WinHttpWebSocketReceive que esté esperando.
		if (HINTERNET ws = ws_.exchange(nullptr))
			WinHttpCloseHandle(ws);
	}

private:
	HINTERNET connection_;
	std::atomic<HINTERNET> ws_;
	std::mutex sendMutex_;
};

} // namespace

Response request(const std::string &method, const std::string &url, const Headers &headers, const std::string &body,
		 int timeoutMs)
{
	Response res;
	HINTERNET connection = nullptr;
	Handle req(openRequest(method.c_str(), url, headers, body, timeoutMs, false, connection, res.error));
	Handle conn(connection);
	if (!req)
		return res;
	res.status = statusCode(req);
	char buffer[16384];
	for (;;) {
		DWORD read = 0;
		if (!WinHttpReadData(req, buffer, sizeof(buffer), &read)) {
			res.error = lastError("lectura");
			res.status = 0;
			return res;
		}
		if (read == 0)
			break;
		res.body.append(buffer, read);
		if (res.body.size() > kMaxBody) {
			res.error = "respuesta demasiado grande";
			res.status = 0;
			return res;
		}
	}
	return res;
}

std::shared_ptr<WebSocket> connectWebSocket(const std::string &url, const Headers &headers, std::string &error)
{
	HINTERNET connection = nullptr;
	Handle req(openRequest("GET", url, headers, {}, 15000, true, connection, error));
	Handle conn(connection);
	if (!req)
		return nullptr;
	const int status = statusCode(req);
	if (status != 101) {
		error = "el servidor rechazó el WebSocket (HTTP " + std::to_string(status) + ")";
		return nullptr;
	}
	HINTERNET ws = WinHttpWebSocketCompleteUpgrade(req, 0);
	if (!ws) {
		error = lastError("WebSocket");
		return nullptr;
	}
	return std::make_shared<WinWebSocket>(conn.release(), ws);
}

} // namespace net
