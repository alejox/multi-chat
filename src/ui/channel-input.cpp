// Channel field: validation, profile preview and confirmation of channels that were not found.

#include "channel-input.hpp"
#include "chat-dock.hpp"
#include "first-run-wizard.hpp"
#include "image-cache.hpp"
#include "profile-cache.hpp"
#include "ui-common.hpp"
#include "../i18n.hpp"
#include "../chat-manager.hpp"
#include "../chat/message.hpp"
#include "../updater.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QDropEvent>
#include <QHash>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace ui {

QString entryErrorText(chat::EntryError code, const QString &arg)
{
	const char *key = chat::entryErrorKey(code);
	return key ? i18n::tr(key).arg(arg) : QString();
}

QPixmap chatAvatar(const chat::SourceInfo &info, int size, qreal dpr)
{
	chat::Profile profile;
	QImage photo;
	if (!chat::isDemo(info) && ProfileCache::instance().get(info, profile) && !profile.avatar.isEmpty())
		photo = ImageCache::instance().get(profile.avatar);
	return ui::avatarPixmap(photo, info.platform, size, dpr);
}

} // namespace ui

void ChannelInput::setPlatform(const QString &platform)
{
	const int index = platform_->findData(platform);
	if (index >= 0)
		platform_->setCurrentIndex(index);
}

ChannelInput::ChannelInput(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	auto *addRow = new QHBoxLayout();
	platform_ = new QComboBox(this);
	for (const char *p : {"twitch", "youtube", "kick", "tiktok", "starvios"})
		platform_->addItem(QIcon(ui::platformIcon(p, 16, devicePixelRatioF())), chat::platformName(p),
				   QString::fromLatin1(p));
	channel_ = new QLineEdit(this);
	channel_->setAccessibleName(i18n::tr("Settings.Chats"));
	add_ = new QPushButton(i18n::tr("Settings.Add"), this);
	add_->setAutoDefault(false);
	addRow->addWidget(platform_);
	addRow->addWidget(channel_, 1);
	addRow->addWidget(add_);
	layout->addLayout(addRow);

	// Preview of the typed or pasted channel: profile photo and name.
	preview_ = new QWidget(this);
	auto *previewLayout = new QHBoxLayout(preview_);
	previewLayout->setContentsMargins(2, 2, 2, 2);
	avatar_ = new QLabel(preview_);
	avatar_->setFixedSize(40, 40);
	previewText_ = new QLabel(preview_);
	previewText_->setTextFormat(Qt::PlainText);
	previewText_->setWordWrap(true);
	// Indeterminate bar while the profile is being looked up.
	busy_ = new QProgressBar(preview_);
	busy_->setRange(0, 0);
	busy_->setTextVisible(false);
	busy_->setFixedWidth(60);
	busy_->setAccessibleName(i18n::tr("Settings.Preview.Loading"));
	previewLayout->addWidget(avatar_);
	previewLayout->addWidget(previewText_, 1);
	previewLayout->addWidget(busy_, 0, Qt::AlignVCenter);
	preview_->hide();
	layout->addWidget(preview_);
	// What is wrong with the typed channel (format, repeated, not found), right under it.
	check_ = new QLabel(this);
	check_->setWordWrap(true);
	check_->setTextFormat(Qt::PlainText);
	{
		QPalette p = check_->palette();
		p.setColor(QPalette::WindowText, ui::alertColor(p.color(QPalette::Window)));
		check_->setPalette(p);
	}
	check_->hide();
	layout->addWidget(check_);
	previewTimer_.setSingleShot(true);
	previewTimer_.setInterval(500);
	connect(&previewTimer_, &QTimer::timeout, this, [this] { updatePreview(); });
	connect(channel_, &QLineEdit::textChanged, this, [this] {
		// A new text needs a new check: no stale "add anyway" nor pending add.
		pendingAdd_.clear();
		notFound_.clear();
		add_->setText(i18n::tr("Settings.Add"));
		add_->setEnabled(true);
		check_->hide();
		previewTimer_.start();
	});
	// The lookup answers in the background: check again when it does.
	refreshTimer_.setSingleShot(true);
	refreshTimer_.setInterval(200);
	connect(&refreshTimer_, &QTimer::timeout, this, [this] { updatePreview(); });
	connect(&ProfileCache::instance(), &ProfileCache::ready, this, [this] { refreshTimer_.start(); });
	connect(&ImageCache::instance(), &ImageCache::ready, this, [this] { refreshTimer_.start(); });

	auto *hintRow = new QHBoxLayout();
	hint_ = new QLabel(this);
	hint_->setWordWrap(true);
	hint_->setForegroundRole(QPalette::PlaceholderText);
	hintRow->addWidget(hint_, 1);
	layout->addLayout(hintRow);
	connect(platform_, &QComboBox::currentIndexChanged, this, [this] {
		updateHint();
		previewTimer_.start();
	});
	// The button confirms a channel that was not found; Enter alone never adds it silently.
	connect(add_, &QPushButton::clicked, this,
		[this] { addEntry(!notFound_.isEmpty() && notFound_ == entryFromInput()); });
	connect(channel_, &QLineEdit::returnPressed, this, [this] { addEntry(false); });
	updateHint();
}

QString ChannelInput::entryFromInput() const
{
	// A pasted URL or "platform:channel" is kept; anything else uses the platform picked in the list.
	return chat::entryFromInput(channel_->text(), platform_->currentData().toString());
}

void ChannelInput::focusInput()
{
	channel_->setFocus();
}

void ChannelInput::updateHint()
{
	const QString p = platform_->currentData().toString();
	channel_->setPlaceholderText(i18n::tr(("Settings.Placeholder." + p).toUtf8().constData()));
	hint_->setText(i18n::tr(("Settings.Hint." + p).toUtf8().constData()));
}

ChannelInput::Validation ChannelInput::validate()
{
	Validation v;
	v.entry = entryFromInput();
	if (v.entry.isEmpty())
		return v;
	chat::EntryError code = chat::EntryError::None;
	QString arg;
	if (!chat::parseEntry(v.entry, v.info, code, arg)) {
		v.state = Check::Invalid;
		v.message = ui::entryErrorText(code, arg);
		return v;
	}
	for (const chat::ChatEntry &e : existing ? existing() : QList<chat::ChatEntry>()) {
		chat::SourceInfo other;
		QString ignored;
		if (chat::parseEntry(e.entry, other, ignored) && other.key == v.info.key) {
			v.state = Check::Duplicate;
			v.message = i18n::tr("Settings.Validate.Duplicate");
			return v;
		}
	}
	if (chat::isDemo(v.info)) { // no profile to look up
		v.state = Check::Found;
		return v;
	}
	if (!ProfileCache::instance().get(v.info, v.profile))
		v.state = Check::Loading;
	else if (v.profile.failed)
		v.state = Check::Unknown;
	else if (v.profile.name.isEmpty() && v.profile.avatar.isEmpty())
		v.state = Check::NotFound;
	else
		v.state = Check::Found;
	return v;
}

void ChannelInput::updatePreview()
{
	const Validation v = validate();
	if (v.state == Check::Empty || v.state == Check::Invalid) {
		preview_->hide();
	} else {
		QString text = chat::platformName(v.info.platform) + QStringLiteral(" · ") + v.info.channel;
		if (v.state == Check::Loading)
			text = i18n::tr("Settings.Preview.Loading") + "\n" + text;
		else if (!v.profile.name.isEmpty())
			text = v.profile.name + "\n" + text;
		previewText_->setText(text);
		avatar_->setPixmap(ui::chatAvatar(v.info, 40, devicePixelRatioF()));
		busy_->setVisible(v.state == Check::Loading);
		preview_->show();
	}

	// A pending add finishes when the lookup answers.
	const bool pending = !pendingAdd_.isEmpty() && pendingAdd_ == v.entry;
	if (pending && (v.state == Check::Found || v.state == Check::Unknown)) {
		commit(v.entry);
		return;
	}
	if (pending && v.state != Check::Loading)
		pendingAdd_.clear();

	QString message = v.message;
	if (v.state == Check::NotFound) {
		message = i18n::tr("Settings.Validate.NotFound").arg(chat::platformName(v.info.platform));
		notFound_ = v.entry;
	} else {
		notFound_.clear();
		if (v.state == Check::Unknown)
			message = i18n::tr("Settings.Validate.Unknown").arg(chat::platformName(v.info.platform));
	}
	check_->setText(message);
	check_->setVisible(!message.isEmpty());
	add_->setText(i18n::tr(v.state == Check::NotFound ? "Settings.AddAnyway"
			    : pendingAdd_.isEmpty()   ? "Settings.Add"
						      : "Settings.Checking"));
	add_->setEnabled(pendingAdd_.isEmpty());
	if (pending && v.state == Check::NotFound)
		add_->setFocus(); // one more press confirms
}

// `confirmed`: the user pressed "Add anyway" for a channel that was not found.
void ChannelInput::addEntry(bool confirmed)
{
	previewTimer_.stop();
	const Validation v = validate();
	switch (v.state) {
	case Check::Empty:
		return;
	case Check::Invalid:
	case Check::Duplicate:
		updatePreview();
		return;
	case Check::Loading:
		// Added as soon as the lookup confirms it exists (or asks again if it does not).
		pendingAdd_ = v.entry;
		updatePreview();
		return;
	case Check::NotFound:
		if (confirmed)
			commit(v.entry);
		else
			updatePreview();
		return;
	case Check::Found:
	case Check::Unknown: // could not check: added as typed, without the "not found" warning
		commit(v.entry);
		return;
	}
}

void ChannelInput::commit(const QString &entry)
{
	pendingAdd_.clear();
	notFound_.clear();
	check_->hide();
	preview_->hide();
	{
		const QSignalBlocker block(channel_);
		channel_->clear();
	}
	add_->setText(i18n::tr("Settings.Add"));
	add_->setEnabled(true);
	if (onAdd)
		onAdd(entry);
}
