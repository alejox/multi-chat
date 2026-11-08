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

#include "chat-view.hpp"

class FlowLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QToolButton;

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
	// Settings…, test messages, time, text size, re-detect, assistant, updates.
	void fillGearMenu(QMenu *menu);
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
// Makes the dock visible and brings it to the front (the assistant does this when it finishes).
void show_multichat_dock();
