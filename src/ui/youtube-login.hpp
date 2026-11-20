#pragma once

// Sign in with Google for YouTube's instant chat: opens the browser on Google's permission page
// and waits, on a local page (http://127.0.0.1:<port>), for Google to send the browser back with
// the code that is exchanged for the session. See chat/youtube-api.hpp.

#include "../chat/youtube-api.hpp"

#include <QObject>
#include <QString>

class QTcpServer;

class YouTubeLogin : public QObject {
	Q_OBJECT
public:
	explicit YouTubeLogin(QObject *parent = nullptr);

	// Starts a sign-in (cancelling one in course). False if the local page could not be opened.
	bool start(const QString &clientId, const QString &clientSecret);
	void cancel();
	bool running() const { return server_ != nullptr; }

signals:
	// The credentials to store (client and refresh token).
	void finished(const chat::youtube::api::Credentials &credentials);
	// Google's or the network's error, as is (for the user to report); empty if cancelled in the browser.
	void failed(const QString &error);

private:
	void onConnection();
	void exchange(const QString &code);

	QTcpServer *server_ = nullptr;
	chat::youtube::api::Credentials pending_;
	QString verifier_;
	QString state_;
	QString redirectUri_;
};
