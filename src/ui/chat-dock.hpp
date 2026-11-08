#pragma once

// Dock "Multichat": estado de cada chat, aviso de multistream y los mensajes de todas
// las plataformas juntos.

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QSet>
#include <QTextBrowser>
#include <QTimer>
#include <QWidget>

class FlowLayout;
class QLabel;
class QLineEdit;
class QPushButton;
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
	int unread_ = 0;
	bool rebuilding_ = false;
};

class ChatDock : public QWidget {
	Q_OBJECT

public:
	explicit ChatDock(QWidget *parent = nullptr);

protected:
	void keyPressEvent(QKeyEvent *event) override;
	void changeEvent(QEvent *event) override;

private:
	// Icons and colors from the current OBS theme (palette).
	void applyTheme();
	void updateState(const QJsonObject &state);
	void changeFont(int delta);
	void showMenu(const QJsonObject &message, const QPoint &globalPos);
	void clearWithUndo();
	void openSearch();
	void closeSearch();
	void updateSearchCount();
	void setShowTimestamps(bool show);

	QLabel *status_ = nullptr;
	QLabel *statusEye_ = nullptr;     // tinted eye icon before the total viewers
	QLabel *statusViewers_ = nullptr; // total viewers
	QWidget *banner_ = nullptr;
	QLabel *bannerText_ = nullptr;
	QList<QLabel *> bannerIcons_;
	QWidget *chips_ = nullptr;
	FlowLayout *chipsLayout_ = nullptr;
	QHash<QString, QToolButton *> chipByKey_; // updated in place, by source key
	QList<QPair<QToolButton *, QString>> iconButtons_; // button, icon name
	QWidget *update_ = nullptr;
	QLabel *errors_ = nullptr;
	QWidget *empty_ = nullptr;
	ChatView *view_ = nullptr;
	QWidget *undoBar_ = nullptr;
	QWidget *searchBar_ = nullptr;
	QLineEdit *searchEdit_ = nullptr;
	QLabel *searchCount_ = nullptr;
	bool timestamps_ = false;
	QTimer undoTimer_;
	QSet<QString> hidden_;
	int fontSize_ = 11;
};

void open_multichat_settings(QWidget *parent);
