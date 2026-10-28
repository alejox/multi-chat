// Red en Linux con libcurl (OBS ya depende de ella). curl hace la conexión TCP/TLS del
// WebSocket (CONNECT_ONLY) y aquí van el saludo HTTP y los marcos (RFC 6455): así no hace falta
// que la libcurl del sistema traiga soporte de WebSocket.

#include "net.hpp"

#include <curl/curl.h>
#include <poll.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>

namespace net {

const char *const kUserAgent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
			       "(KHTML, like Gecko) Chrome/130.0 Safari/537.36";

namespace {

constexpr size_t kMaxBody = 16 * 1024 * 1024;

void globalInit()
{
	static std::once_flag once;
	std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct Easy {
	CURL *h = curl_easy_init();
	curl_slist *headers = nullptr;
	Easy() = default;
	Easy(const Easy &) = delete;
	Easy &operator=(const Easy &) = delete;
	~Easy()
	{
		if (h)
			curl_easy_cleanup(h);
		curl_slist_free_all(headers);
	}
};

struct Transfer {
	Response *res;
	bool tooBig = false;
};

size_t onBody(char *data, size_t size, size_t count, void *user)
{
	auto *t = static_cast<Transfer *>(user);
	const size_t n = size * count;
	if (t->res->body.size() + n > kMaxBody) {
		t->tooBig = true;
		return 0; // corta la transferencia
	}
	t->res->body.append(data, n);
	return n;
}

size_t onHeader(char *data, size_t size, size_t count, void *user)
{
	auto *t = static_cast<Transfer *>(user);
	const size_t n = size * count;
	std::string line(data, n);
	while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
		line.pop_back();
	// Con redirecciones llegan las cabeceras de cada respuesta: solo valen las de la última.
	if (line.rfind("HTTP/", 0) == 0) {
		t->res->headers.clear();
		return n;
	}
	const size_t colon = line.find(':');
	if (colon == std::string::npos)
		return n;
	size_t v = colon + 1;
	while (v < line.size() && line[v] == ' ')
		++v;
	t->res->headers.emplace_back(line.substr(0, colon), line.substr(v));
	return n;
}

void common(CURL *h, const std::string &url, long timeoutMs)
{
	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(h, CURLOPT_USERAGENT, kUserAgent);
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, timeoutMs);
}

// Datos del WebSocket: host, puerto y ruta, y la URL https:// que curl sabe conectar.
struct WsUrl {
	std::string connect, host, path;
	bool ok = false;
};

WsUrl parseWsUrl(const std::string &url)
{
	WsUrl out;
	CURLU *u = curl_url();
	if (!u)
		return out;
	std::string fixed = url;
	if (fixed.rfind("wss://", 0) == 0)
		fixed = "https://" + fixed.substr(6);
	else if (fixed.rfind("ws://", 0) == 0)
		fixed = "http://" + fixed.substr(5);
	char *scheme = nullptr, *host = nullptr, *port = nullptr, *path = nullptr, *query = nullptr;
	if (curl_url_set(u, CURLUPART_URL, fixed.c_str(), 0) == CURLUE_OK &&
	    curl_url_get(u, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK &&
	    curl_url_get(u, CURLUPART_HOST, &host, 0) == CURLUE_OK &&
	    curl_url_get(u, CURLUPART_PATH, &path, 0) == CURLUE_OK) {
		curl_url_get(u, CURLUPART_PORT, &port, 0); // solo si la URL lo trae
		curl_url_get(u, CURLUPART_QUERY, &query, 0);
		out.host = host;
		if (port)
			out.host += std::string(":") + port;
		out.path = path;
		if (query)
			out.path += std::string("?") + query;
		out.connect = std::string(scheme) + "://" + out.host + "/";
		out.ok = true;
	}
	curl_free(scheme);
	curl_free(host);
	curl_free(port);
	curl_free(path);
	curl_free(query);
	curl_url_cleanup(u);
	return out;
}

std::string base64(const unsigned char *data, size_t size)
{
	static const char *const table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	for (size_t i = 0; i < size; i += 3) {
		const unsigned v = unsigned(data[i]) << 16 | (i + 1 < size ? unsigned(data[i + 1]) << 8 : 0u) |
				   (i + 2 < size ? unsigned(data[i + 2]) : 0u);
		out += table[(v >> 18) & 63];
		out += table[(v >> 12) & 63];
		out += i + 1 < size ? table[(v >> 6) & 63] : '=';
		out += i + 2 < size ? table[v & 63] : '=';
	}
	return out;
}

class CurlWebSocket : public WebSocket {
public:
	CurlWebSocket(CURL *h, curl_socket_t socket, std::string pending)
		: h_(h),
		  socket_(socket),
		  in_(std::move(pending)),
		  random_(std::random_device{}())
	{
	}
	~CurlWebSocket() override
	{
		close();
		curl_easy_cleanup(h_);
	}

	Result receive(std::string &message) override
	{
		message.clear();
		int messageType = -1; // tipo del mensaje en curso (texto o binario)
		for (;;) {
			unsigned char opcode = 0;
			bool fin = false;
			std::string payload;
			if (!readFrame(opcode, fin, payload))
				return Result::Closed;
			switch (opcode) {
			case 0x8: // el servidor cierra: se le responde y se termina
				sendFrame(0x8, payload.substr(0, 2));
				closed_ = true;
				return Result::Closed;
			case 0x9: // ping → pong con los mismos datos
				if (!sendFrame(0xA, payload))
					return Result::Closed;
				continue;
			case 0xA: // pong
				continue;
			case 0x0: // continuación de un mensaje en curso
				if (messageType < 0)
					return Result::Closed;
				break;
			case 0x1:
			case 0x2:
				if (messageType >= 0)
					return Result::Closed;
				messageType = opcode;
				break;
			default:
				return Result::Closed;
			}
			message += payload;
			if (message.size() > kMaxBody)
				return Result::Closed;
			if (fin)
				return Result::Message;
		}
	}

	bool send(const std::string &text) override { return sendFrame(0x1, text); }
	bool sendBinary(const std::string &data) override { return sendFrame(0x2, data); }

	// receive() lo nota en menos de una décima de segundo.
	void close() override { closed_ = true; }

private:
	// Espera a que el socket se pueda leer o escribir, como mucho 100 ms.
	bool wait(short events)
	{
		pollfd pfd = {};
		pfd.fd = int(socket_);
		pfd.events = events;
		return poll(&pfd, 1, 100) >= 0;
	}

	// Lee lo que haya (sin bloquear: la conexión TLS se comparte con send()).
	bool fill()
	{
		for (;;) {
			if (closed_)
				return false;
			char buffer[16384];
			size_t got = 0;
			CURLcode rc;
			{
				std::lock_guard<std::mutex> lock(mutex_);
				rc = curl_easy_recv(h_, buffer, sizeof(buffer), &got);
			}
			if (rc == CURLE_OK) {
				if (got == 0) // el servidor cerró la conexión
					return false;
				in_.append(buffer, got);
				return true;
			}
			if (rc != CURLE_AGAIN || !wait(POLLIN))
				return false;
		}
	}

	bool need(size_t bytes)
	{
		while (in_.size() < bytes)
			if (!fill())
				return false;
		return true;
	}

	bool readFrame(unsigned char &opcode, bool &fin, std::string &payload)
	{
		if (!need(2))
			return false;
		const auto byte = [this](size_t i) {
			return static_cast<unsigned char>(in_[i]);
		};
		fin = byte(0) & 0x80;
		opcode = byte(0) & 0x0F;
		const bool masked = byte(1) & 0x80;
		uint64_t length = byte(1) & 0x7F;
		size_t header = 2;
		if (length == 126) {
			if (!need(4))
				return false;
			length = uint64_t(byte(2)) << 8 | byte(3);
			header = 4;
		} else if (length == 127) {
			if (!need(10))
				return false;
			length = 0;
			for (size_t i = 2; i < 10; ++i)
				length = length << 8 | byte(i);
			header = 10;
		}
		if (length > kMaxBody)
			return false;
		const size_t maskAt = header;
		if (masked)
			header += 4;
		if (!need(header + size_t(length)))
			return false;
		payload = in_.substr(header, size_t(length));
		if (masked)
			for (size_t i = 0; i < payload.size(); ++i)
				payload[i] = char(payload[i] ^ in_[maskAt + i % 4]);
		in_.erase(0, header + size_t(length));
		return true;
	}

	// Lo que manda el cliente va siempre enmascarado.
	bool sendFrame(unsigned char opcode, const std::string &data)
	{
		std::string frame;
		frame += char(0x80 | opcode);
		const size_t n = data.size();
		if (n < 126) {
			frame += char(0x80 | n);
		} else if (n <= 0xFFFF) {
			frame += char(0x80 | 126);
			frame += char(n >> 8);
			frame += char(n & 0xFF);
		} else {
			frame += char(0x80 | 127);
			for (int shift = 56; shift >= 0; shift -= 8)
				frame += char((uint64_t(n) >> shift) & 0xFF);
		}
		std::lock_guard<std::mutex> lock(mutex_);
		unsigned char mask[4];
		for (unsigned char &m : mask)
			m = static_cast<unsigned char>(random_() & 0xFF);
		frame.append(reinterpret_cast<const char *>(mask), 4);
		const size_t start = frame.size();
		frame += data;
		for (size_t i = start; i < frame.size(); ++i)
			frame[i] = char(frame[i] ^ mask[(i - start) % 4]);
		return sendAll(frame);
	}

	// Con mutex_ tomado.
	bool sendAll(const std::string &data)
	{
		size_t offset = 0;
		while (offset < data.size()) {
			size_t sent = 0;
			const CURLcode rc = curl_easy_send(h_, data.data() + offset, data.size() - offset, &sent);
			if (rc == CURLE_OK) {
				offset += sent;
			} else if (rc != CURLE_AGAIN || closed_ || !wait(POLLOUT)) {
				return false;
			}
		}
		return true;
	}

	CURL *h_;
	curl_socket_t socket_;
	std::string in_;
	std::atomic<bool> closed_{false};
	std::mutex mutex_;
	std::mt19937 random_;

	friend std::shared_ptr<WebSocket> net::connectWebSocket(const std::string &, const Headers &, std::string &);
};

} // namespace

Response request(const std::string &method, const std::string &url, const Headers &headers, const std::string &body,
		 int timeoutMs)
{
	globalInit();
	Response res;
	Easy easy;
	if (!easy.h) {
		res.error = "curl_easy_init";
		return res;
	}
	CURL *h = easy.h;
	common(h, url, timeoutMs);
	curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, long(timeoutMs));
	curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(h, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, ""); // gzip/deflate/br según la libcurl
	for (const auto &[name, value] : headers)
		easy.headers = curl_slist_append(easy.headers, (name + ": " + value).c_str());
	if (easy.headers)
		curl_easy_setopt(h, CURLOPT_HTTPHEADER, easy.headers);
	if (method == "GET") {
		curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
	} else {
		if (method == "POST" || !body.empty()) {
			curl_easy_setopt(h, CURLOPT_POSTFIELDS, body.data());
			curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(body.size()));
		}
		if (method != "POST")
			curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method.c_str());
	}
	Transfer transfer{&res};
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, onBody);
	curl_easy_setopt(h, CURLOPT_WRITEDATA, &transfer);
	curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, onHeader);
	curl_easy_setopt(h, CURLOPT_HEADERDATA, &transfer);
	const CURLcode rc = curl_easy_perform(h);
	if (rc != CURLE_OK) {
		res.error = transfer.tooBig ? "respuesta demasiado grande" : curl_easy_strerror(rc);
		res.status = 0;
		return res;
	}
	long status = 0;
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
	res.status = int(status);
	return res;
}

std::shared_ptr<WebSocket> connectWebSocket(const std::string &url, const Headers &headers, std::string &error)
{
	globalInit();
	const WsUrl u = parseWsUrl(url);
	if (!u.ok) {
		error = "URL no válida: " + url;
		return nullptr;
	}
	CURL *h = curl_easy_init();
	if (!h) {
		error = "curl_easy_init";
		return nullptr;
	}
	common(h, u.connect, 15000);
	curl_easy_setopt(h, CURLOPT_CONNECT_ONLY, 1L);
	// El saludo del WebSocket es HTTP/1.1: que el TLS no acuerde HTTP/2.
	curl_easy_setopt(h, CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_1_1));
	CURLcode rc = curl_easy_perform(h);
	curl_socket_t socket = CURL_SOCKET_BAD;
	if (rc == CURLE_OK)
		rc = curl_easy_getinfo(h, CURLINFO_ACTIVESOCKET, &socket);
	if (rc != CURLE_OK || socket == CURL_SOCKET_BAD) {
		error = std::string("no se pudo conectar: ") + curl_easy_strerror(rc);
		curl_easy_cleanup(h);
		return nullptr;
	}

	unsigned char key[16];
	std::random_device random;
	for (unsigned char &k : key)
		k = static_cast<unsigned char>(random() & 0xFF);
	std::string hello = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host +
			    "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
			    "Sec-WebSocket-Key: " +
			    base64(key, sizeof(key)) + "\r\nUser-Agent: " + kUserAgent + "\r\n";
	for (const auto &[name, value] : headers)
		hello += name + ": " + value + "\r\n";
	hello += "\r\n";

	auto ws = std::make_shared<CurlWebSocket>(h, socket, std::string());
	{
		std::lock_guard<std::mutex> lock(ws->mutex_);
		if (!ws->sendAll(hello)) {
			error = "no se pudo enviar el saludo del WebSocket";
			return nullptr;
		}
	}
	// La respuesta llega hasta una línea vacía; lo que sobre ya son marcos.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
	size_t end;
	while ((end = ws->in_.find("\r\n\r\n")) == std::string::npos) {
		if (ws->in_.size() > 65536 || std::chrono::steady_clock::now() > deadline || !ws->fill()) {
			error = "el servidor no respondió al WebSocket";
			return nullptr;
		}
	}
	const std::string response = ws->in_.substr(0, end);
	ws->in_.erase(0, end + 4);
	int status = 0;
	const size_t space = response.find(' ');
	if (space != std::string::npos)
		status = std::atoi(response.c_str() + space + 1);
	if (status != 101) {
		error = "el servidor rechazó el WebSocket (HTTP " + std::to_string(status) + ")";
		return nullptr;
	}
	return ws;
}

} // namespace net
