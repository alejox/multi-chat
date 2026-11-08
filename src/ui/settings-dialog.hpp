#pragma once

// The "Multichat" settings window and the channel field it shares with the first-run wizard.

#include "../chat/platforms.hpp"

#include <QPixmap>
#include <QTimer>
#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace ui {
// Translated text of an entry error (data/locale: Entry.Error.*).
QString entryErrorText(chat::EntryError code, const QString &arg);
// The channel's profile photo, or the platform logo while it loads or if there is none.
QPixmap chatAvatar(const chat::SourceInfo &info, int size, qreal dpr);
} // namespace ui

// Platform picker, channel field and "Add" button. Shows the channel's profile while it is looked
// up, explains format errors and repeated chats, and asks to confirm a channel that was not found.
class ChannelInput : public QWidget {
public:
	explicit ChannelInput(QWidget *parent);

	// Chats already configured (to reject repeats).
	std::function<QList<chat::ChatEntry>()> existing;
	// A valid entry the user wants to add.
	std::function<void(const QString &entry)> onAdd;

	void focusInput();
	// Selects a platform in the list ("twitch", "youtube"...); unknown names are ignored.
	void setPlatform(const QString &platform);

private:
	// Unknown: the lookup failed (network, server), so it is not known whether the channel exists.
	enum class Check { Empty, Invalid, Duplicate, Loading, Found, NotFound, Unknown };
	struct Validation {
		Check state = Check::Empty;
		QString entry;
		chat::SourceInfo info;
		chat::Profile profile;
		QString message; // why it cannot be added (Invalid, Duplicate)
	};

	QString entryFromInput() const;
	void updateHint();
	Validation validate();
	void updatePreview();
	void addEntry(bool confirmed);
	void commit(const QString &entry);

	QComboBox *platform_ = nullptr;
	QLineEdit *channel_ = nullptr;
	QPushButton *add_ = nullptr;
	QLabel *hint_ = nullptr;
	QWidget *preview_ = nullptr;
	QLabel *avatar_ = nullptr;
	QLabel *previewText_ = nullptr;
	QProgressBar *busy_ = nullptr;
	QLabel *check_ = nullptr;
	QString pendingAdd_; // entry to add once its lookup answers
	QString notFound_;   // entry shown as "not found": the button confirms it
	QTimer previewTimer_;
	QTimer refreshTimer_;
};

void open_multichat_settings(QWidget *parent);
