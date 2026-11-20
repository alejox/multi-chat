#include "youtube-login.hpp"
#include "../i18n.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <thread>

using chat::youtube::api::Credentials;
namespace api = chat::youtube::api;

YouTubeLogin::YouTubeLogin(QObject *parent) : QObject(parent) {}

bool YouTubeLogin::start(const QString &clientId, const QString &clientSecret)
{
	cancel();
	server_ = new QTcpServer(this);
	// Solo esta máquina, en un puerto libre cualquiera: Google lo admite para apps de escritorio.
	if (!server_->listen(QHostAddress::LocalHost, 0)) {
		cancel();
		return false;
	}
	pending_ = Credentials{clientId.trimmed(), clientSecret.trimmed(), QString()};
	verifier_ = api::makeVerifier();
	state_ = api::makeVerifier().left(24);
	redirectUri_ = QStringLiteral("http://127.0.0.1:%1").arg(server_->serverPort());
	connect(server_, &QTcpServer::newConnection, this, &YouTubeLogin::onConnection);
	// Si nadie termina el inicio de sesión, la página local no se queda abierta para siempre.
	QTimer::singleShot(10 * 60 * 1000, server_, [this] { cancel(); });
	QDesktopServices::openUrl(
		QUrl(api::authUrl(pending_.clientId, redirectUri_, api::challengeFor(verifier_), state_)));
	return true;
}

void YouTubeLogin::cancel()
{
	if (!server_)
		return;
	server_->close();
	server_->deleteLater();
	server_ = nullptr;
}

void YouTubeLogin::onConnection()
{
	while (server_ && server_->hasPendingConnections()) {
		QTcpSocket *socket = server_->nextPendingConnection();
		connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
			if (!socket->canReadLine())
				return;
			const QByteArray line = socket->readLine();
			QString code, state, error;
			const bool answer = api::parseRedirect(line, code, state, error);
			// El navegador también pide /favicon.ico y cosas así: esas no son la respuesta.
			const bool ours = answer && server_ && state == state_;
			const QString text = !ours ? QStringLiteral("AC Multichat")
						   : i18n::tr(code.isEmpty() ? "YouTubeLogin.PageFailed" : "YouTubeLogin.PageDone");
			const QByteArray body =
				"<!doctype html><meta charset=\"utf-8\"><title>AC Multichat</title>"
				"<body style=\"font-family:system-ui,sans-serif;background:#16171d;color:#e9eaf2;"
				"display:flex;align-items:center;justify-content:center;height:100vh;margin:0\">"
				"<p style=\"font-size:20px;max-width:32em;text-align:center\">" +
				text.toHtmlEscaped().toUtf8() + "</p></body>";
			socket->write("HTTP/1.1 " + QByteArray(ours ? "200 OK" : "404 Not Found") +
				      "\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
				      QByteArray::number(body.size()) + "\r\n\r\n" + body);
			socket->disconnectFromHost();
			if (!ours)
				return;
			cancel();
			if (code.isEmpty())
				emit failed(error == QLatin1String("access_denied") ? QString() : error);
			else
				exchange(code);
		});
	}
}

void YouTubeLogin::exchange(const QString &code)
{
	// La petición a Google bloquea: va en un hilo y el resultado vuelve al hilo de la interfaz.
	const Credentials credentials = pending_;
	const QString verifier = verifier_, redirectUri = redirectUri_;
	QPointer<YouTubeLogin> self(this);
	std::thread([self, credentials, code, verifier, redirectUri] {
		const api::Tokens tokens = api::exchangeCode(credentials, code, verifier, redirectUri);
		QMetaObject::invokeMethod(
			qApp,
			[self, credentials, tokens] {
				if (!self)
					return;
				if (tokens.refreshToken.isEmpty()) {
					emit self->failed(tokens.error.isEmpty() ? QStringLiteral("no refresh token")
										 : tokens.error);
					return;
				}
				Credentials done = credentials;
				done.refreshToken = tokens.refreshToken;
				emit self->finished(done);
			},
			Qt::QueuedConnection);
	}).detach();
}
