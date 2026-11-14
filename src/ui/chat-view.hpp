#pragma once

// The dock's message view: renders the chat as rich text, downloads emotes on demand and keeps the
// scroll position when messages are added, removed or rebuilt.

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QTextBrowser>

class QLabel;
class QToolButton;

// Vista de mensajes. Los emotes se descargan aparte y se pintan cuando llegan.
class ChatView : public QTextBrowser {
	Q_OBJECT

public:
	explicit ChatView(QWidget *parent = nullptr);

	void setHiddenSources(const QSet<QString> &hidden);
	void setFontSize(int pointSize);
	void addMessage(const QJsonObject &message);
	void setMessages(const QList<QJsonObject> &messages);
	// Removes the messages a moderation event affects, keeping the scroll position.
	void removeMessages(const QJsonObject &deletion);
	// Resalta el mensaje destacado (id vacío: ninguno).
	void setFeatured(const QString &id);
	// Barra arriba de la lista con el destacado: su autor, pausa (sin tiempo) y quitar. Vacío la oculta.
	void setFeaturedBar(const QJsonObject &message, bool paused);
	// Jumps to the newest message and hides the "new messages" pill.
	void scrollToBottom();
	// Shows only the messages matching the query (chat::matchesSearch); empty shows all.
	void setSearch(const QString &query);
	void setShowTimestamps(bool show);
	// Messages currently on screen (after hidden chats and the search).
	int shownCount() const;

signals:
	// Clic en un mensaje (no en un enlace ni al seleccionar texto).
	void messageClicked(const QJsonObject &message);
	// Right click; `message` is empty when not on a message.
	void menuRequested(const QJsonObject &message, const QPoint &globalPos);
	// Messages were added, removed or rebuilt.
	void contentChanged();
	// Botones de la barra del destacado.
	void featuredPauseClicked(bool paused);
	void featuredRemoveClicked();

protected:
	QVariant loadResource(int type, const QUrl &name) override;
	void changeEvent(QEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	// Copying a selection gives plain text only.
	QMimeData *createMimeDataFromSelection() const override;

private:
	QString html(const QJsonObject &message) const;
	void rebuild();
	bool isAtBottom() const;
	void imageReady(const QString &url);
	void updatePill();
	void placeFeaturedBar();
	QColor featuredColor() const;
	QString messageIdAt(const QPoint &pos) const;
	QJsonObject messageAt(const QPoint &pos) const;

	QList<QJsonObject> messages_;
	QString featuredId_;
	QPoint pressPos_;
	QSet<QString> hidden_;
	QString search_;
	bool timestamps_ = false;
	int emoteSize_ = 22;
	QToolButton *pill_ = nullptr; // "↓ N new" while scrolled up
	QWidget *featuredBar_ = nullptr;
	QLabel *featuredLabel_ = nullptr;
	QToolButton *featuredPause_ = nullptr;
	int unread_ = 0;
	bool rebuilding_ = false;
};
