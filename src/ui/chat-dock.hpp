#pragma once

// Dock "Multichat": estado de cada chat, aviso de multistream y los mensajes de todas
// las plataformas juntos.

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QTextBrowser>
#include <QWidget>

class FlowLayout;
class QLabel;
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
	// Resalta el mensaje destacado (id vacío: ninguno).
	void setFeatured(const QString &id);

signals:
	// Clic en un mensaje (no en un enlace ni al seleccionar texto).
	void messageClicked(const QJsonObject &message);

protected:
	QVariant loadResource(int type, const QUrl &name) override;
	void changeEvent(QEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	QString html(const QJsonObject &message) const;
	void rebuild();
	QColor featuredColor() const;
	QString messageIdAt(const QPoint &pos) const;

	QList<QJsonObject> messages_;
	QString featuredId_;
	QPoint pressPos_;
	QSet<QString> hidden_;
	int emoteSize_ = 22;
};

class ChatDock : public QWidget {
	Q_OBJECT

public:
	explicit ChatDock(QWidget *parent = nullptr);

protected:
	void keyPressEvent(QKeyEvent *event) override;

private:
	void updateState(const QJsonObject &state);
	void changeFont(int delta);

	QLabel *status_ = nullptr;
	QWidget *banner_ = nullptr;
	QLabel *bannerText_ = nullptr;
	QList<QLabel *> bannerIcons_;
	QWidget *chips_ = nullptr;
	FlowLayout *chipsLayout_ = nullptr;
	QLabel *errors_ = nullptr;
	QWidget *empty_ = nullptr;
	ChatView *view_ = nullptr;
	QSet<QString> hidden_;
	int fontSize_ = 11;
};

void open_multichat_settings(QWidget *parent);
