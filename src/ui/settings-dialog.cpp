// "Multichat" window: add, pause, reorder and remove chats, and choose when to connect them.

#include "settings-dialog.hpp"
#include "chat-dock.hpp"
#include "first-run-wizard.hpp"
#include "image-cache.hpp"
#include "profile-cache.hpp"
#include "ui-common.hpp"
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
	return key ? ui::T(key).arg(arg) : QString();
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
	channel_->setAccessibleName(ui::T("Settings.Chats"));
	add_ = new QPushButton(ui::T("Settings.Add"), this);
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
	busy_->setAccessibleName(ui::T("Settings.Preview.Loading"));
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
		add_->setText(ui::T("Settings.Add"));
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
	channel_->setPlaceholderText(ui::T(("Settings.Placeholder." + p).toUtf8().constData()));
	hint_->setText(ui::T(("Settings.Hint." + p).toUtf8().constData()));
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
			v.message = ui::T("Settings.Validate.Duplicate");
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
			text = ui::T("Settings.Preview.Loading") + "\n" + text;
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
		message = ui::T("Settings.Validate.NotFound").arg(chat::platformName(v.info.platform));
		notFound_ = v.entry;
	} else {
		notFound_.clear();
		if (v.state == Check::Unknown)
			message = ui::T("Settings.Validate.Unknown").arg(chat::platformName(v.info.platform));
	}
	check_->setText(message);
	check_->setVisible(!message.isEmpty());
	add_->setText(ui::T(v.state == Check::NotFound ? "Settings.AddAnyway"
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
	add_->setText(ui::T("Settings.Add"));
	add_->setEnabled(true);
	if (onAdd)
		onAdd(entry);
}

namespace {

// The chat list: tells the dialog when a drag finished so the new order is saved.
class ChatList : public QListWidget {
public:
	using QListWidget::QListWidget;
	std::function<void()> onDropped;

protected:
	void dropEvent(QDropEvent *event) override
	{
		QListWidget::dropEvent(event);
		if (onDropped)
			onDropped();
	}
};

class SettingsDialog : public QDialog {
public:
	explicit SettingsDialog(QWidget *parent) : QDialog(parent)
	{
		setWindowTitle(ui::T("Settings.Title"));
		setAttribute(Qt::WA_DeleteOnClose);
		setMinimumWidth(460);
		auto *root = new QVBoxLayout(this);
		auto *tabs = new QTabWidget(this);
		auto *chatsPage = new QWidget(tabs);
		auto *chatsPageLayout = new QVBoxLayout(chatsPage);
		tabs->addTab(chatsPage, ui::T("Settings.Tab.Chats"));
		tabs->addTab(buildFilters(tabs), ui::T("Settings.Tab.Filters"));
		root->addWidget(tabs);

		// Lista de chats.
		auto *chatsBox = new QGroupBox(ui::T("Settings.Chats"), chatsPage);
		auto *chatsLayout = new QVBoxLayout(chatsBox);
		list_ = new ChatList(chatsBox);
		list_->setMinimumHeight(160);
		list_->setDragDropMode(QAbstractItemView::InternalMove);
		list_->setDefaultDropAction(Qt::MoveAction);
		list_->setSelectionMode(QAbstractItemView::SingleSelection);
		list_->setAccessibleName(ui::T("Settings.Chats"));
		list_->onDropped = [this] {
			QMetaObject::invokeMethod(this, [this] { syncOrderFromList(); }, Qt::QueuedConnection);
		};
		chatsLayout->addWidget(list_);
		auto *listHelp = new QLabel(ui::T("Settings.ListHelp"), chatsBox);
		listHelp->setWordWrap(true);
		listHelp->setForegroundRole(QPalette::PlaceholderText);
		chatsLayout->addWidget(listHelp);

		input_ = new ChannelInput(chatsBox);
		input_->existing = [this] { return entries_; };
		input_->onAdd = [this](const QString &entry) {
			error_->hide();
			entries_ << chat::ChatEntry{entry, true};
			apply();
		};
		chatsLayout->addWidget(input_);
		// Profiles and photos arrive in the background (and the dock asks the same cache for emotes):
		// grouped into a single update.
		refreshTimer_.setSingleShot(true);
		refreshTimer_.setInterval(200);
		connect(&refreshTimer_, &QTimer::timeout, this, [this] { refreshList(); });
		connect(&ProfileCache::instance(), &ProfileCache::ready, this, [this] { refreshTimer_.start(); });
		connect(&ImageCache::instance(), &ImageCache::ready, this, [this] { refreshTimer_.start(); });
		error_ = new QLabel(chatsBox);
		error_->setWordWrap(true);
		{
			QPalette p = error_->palette();
			p.setColor(QPalette::WindowText, ui::alertColor(p.color(QPalette::Window)));
			error_->setPalette(p);
		}
		error_->hide();
		chatsLayout->addWidget(error_);
		chatsPageLayout->addWidget(chatsBox);

		// Cuándo conectar.
		auto *modeBox = new QGroupBox(ui::T("Settings.When"), chatsPage);
		auto *modeLayout = new QVBoxLayout(modeBox);
		auto_ = new QRadioButton(ui::T("Settings.Mode.Auto"), modeBox);
		always_ = new QRadioButton(ui::T("Settings.Mode.Always"), modeBox);
		auto *autoHelp = new QLabel(ui::T("Settings.Mode.AutoHelp"), modeBox);
		autoHelp->setWordWrap(true);
		autoHelp->setForegroundRole(QPalette::PlaceholderText);
		autoHelp->setContentsMargins(22, 0, 0, 0);
		modeLayout->addWidget(auto_);
		modeLayout->addWidget(autoHelp);
		modeLayout->addWidget(always_);
		demo_ = new QCheckBox(ui::T("Settings.Demo"), modeBox);
		modeLayout->addWidget(demo_);
		chatsPageLayout->addWidget(modeBox);
		chatsPageLayout->addStretch(1);

		// Shortcuts that used to be Tools menu entries or a text telling where to click.
		auto *actions = new QHBoxLayout();
		auto *addSources = new QPushButton(ui::T("Settings.AddSources"), this);
		addSources->setToolTip(ui::T("Settings.AddSources.Help"));
		auto *wizard = new QPushButton(ui::T("Settings.Wizard"), this);
		auto *updates = new QPushButton(ui::T("Settings.CheckUpdates"), this);
		for (QPushButton *b : {addSources, wizard, updates})
			b->setAutoDefault(false);
		actions->addWidget(addSources);
		actions->addWidget(wizard);
		actions->addWidget(updates);
		actions->addStretch(1);
		root->addLayout(actions);
		auto *addResult = new QLabel(this);
		addResult->setWordWrap(true);
		addResult->setTextFormat(Qt::PlainText);
		addResult->hide();
		root->addWidget(addResult);
		connect(addSources, &QPushButton::clicked, this, [addResult] {
			addResult->setText(ui::addMultichatSources(true, true, true).join('\n'));
			addResult->show();
		});
		connect(wizard, &QPushButton::clicked, this, [this] { open_first_run_wizard(this); });
		connect(updates, &QPushButton::clicked, this, [] { Updater::instance().check(true); });

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
		root->addWidget(buttons);

		ChatManager &m = ChatManager::instance();
		entries_ = m.chats();
		(m.mode() == ChatManager::Mode::Always ? always_ : auto_)->setChecked(true);
		demo_->setChecked(m.testMessages());

		auto *removeKey = new QShortcut(QKeySequence::Delete, list_, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
		connect(removeKey, &QShortcut::activated, this, [this] {
			if (QListWidgetItem *item = list_->currentItem())
				removeEntry(item->data(Qt::UserRole).toString());
		});
		auto *upKey = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Up), list_, nullptr, nullptr,
					    Qt::WidgetWithChildrenShortcut);
		connect(upKey, &QShortcut::activated, this, [this] { moveCurrent(-1); });
		auto *downKey = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Down), list_, nullptr, nullptr,
					      Qt::WidgetWithChildrenShortcut);
		connect(downKey, &QShortcut::activated, this, [this] { moveCurrent(1); });
		connect(auto_, &QRadioButton::toggled, this, [this] { apply(); });
		connect(demo_, &QCheckBox::toggled, this, [](bool on) { ChatManager::instance().setTestMessages(on); });
		connect(&m, &ChatManager::testMessagesChanged, this, [this](bool on) {
			if (demo_->isChecked() != on)
				demo_->setChecked(on);
		});
		connect(&m, &ChatManager::stateChanged, this, [this] { refreshList(); });

		refreshList();
	}

	~SettingsDialog() override
	{
		// Closed right after an edit: do not lose it.
		if (filtersTimer_.isActive())
			applyFilters();
	}

private:
	// "Filters" tab: hidden users, commands and blocked words. Saved a moment after each change.
	QWidget *buildFilters(QWidget *parent)
	{
		auto *page = new QWidget(parent);
		auto *layout = new QVBoxLayout(page);
		const chat::FilterRules rules = ChatManager::instance().filters();
		auto help = [page](const char *key) {
			auto *label = new QLabel(ui::T(key), page);
			label->setWordWrap(true);
			label->setForegroundRole(QPalette::PlaceholderText);
			return label;
		};
		auto listEdit = [page](const QStringList &items, const char *placeholder) {
			auto *edit = new QPlainTextEdit(page);
			edit->setPlainText(items.join('\n'));
			edit->setPlaceholderText(ui::T(placeholder));
			edit->setTabChangesFocus(true);
			edit->setMinimumHeight(90);
			return edit;
		};

		auto *usersBox = new QGroupBox(ui::T("Settings.Filters.Users"), page);
		auto *usersLayout = new QVBoxLayout(usersBox);
		hiddenUsers_ = listEdit(rules.hiddenUsers, "Settings.Filters.UsersPlaceholder");
		usersLayout->addWidget(hiddenUsers_);
		auto *usersRow = new QHBoxLayout();
		usersRow->addWidget(help("Settings.Filters.UsersHelp"), 1);
		auto *restore = new QPushButton(ui::T("Settings.Filters.RestoreBots"), usersBox);
		usersRow->addWidget(restore, 0, Qt::AlignTop);
		usersLayout->addLayout(usersRow);
		layout->addWidget(usersBox);

		auto *commandsBox = new QGroupBox(ui::T("Settings.Filters.Commands"), page);
		auto *commandsLayout = new QVBoxLayout(commandsBox);
		hideCommands_ = new QCheckBox(ui::T("Settings.Filters.HideCommands"), commandsBox);
		hideCommands_->setChecked(rules.hideCommands);
		commandsLayout->addWidget(hideCommands_);
		commandsLayout->addWidget(help("Settings.Filters.CommandsHelp"));
		layout->addWidget(commandsBox);

		auto *wordsBox = new QGroupBox(ui::T("Settings.Filters.Words"), page);
		auto *wordsLayout = new QVBoxLayout(wordsBox);
		blockedWords_ = listEdit(rules.blockedWords, "Settings.Filters.WordsPlaceholder");
		wordsLayout->addWidget(blockedWords_);
		wordsLayout->addWidget(help("Settings.Filters.WordsHelp"));
		layout->addWidget(wordsBox);
		layout->addWidget(help("Settings.Filters.Note"));
		layout->addStretch(1);

		filtersTimer_.setSingleShot(true);
		filtersTimer_.setInterval(400);
		connect(&filtersTimer_, &QTimer::timeout, this, [this] { applyFilters(); });
		auto changed = [this] { filtersTimer_.start(); };
		connect(hiddenUsers_, &QPlainTextEdit::textChanged, this, changed);
		connect(blockedWords_, &QPlainTextEdit::textChanged, this, changed);
		connect(hideCommands_, &QCheckBox::toggled, this, changed);
		// A user hidden from the dock while this is open: add it here so a later edit keeps it.
		connect(&ChatManager::instance(), &ChatManager::filtersChanged, this, [this] {
			const bool pending = filtersTimer_.isActive();
			filtersTimer_.stop();
			QStringList users = splitList(hiddenUsers_->toPlainText());
			for (const QString &user : ChatManager::instance().filters().hiddenUsers)
				if (!users.contains(user, Qt::CaseInsensitive))
					users << user;
			{
				const QSignalBlocker block(hiddenUsers_);
				hiddenUsers_->setPlainText(users.join('\n'));
			}
			if (pending)
				applyFilters();
		});
		connect(restore, &QPushButton::clicked, this, [this] {
			QStringList users = splitList(hiddenUsers_->toPlainText());
			for (const QString &bot : chat::defaultHiddenUsers())
				if (!users.contains(bot, Qt::CaseInsensitive))
					users << bot;
			hiddenUsers_->setPlainText(users.join('\n'));
		});
		return page;
	}

	// One entry per line (commas also separate), without blanks or repeats.
	static QStringList splitList(const QString &text)
	{
		QStringList out;
		for (QString item : text.split(QRegularExpression(QStringLiteral("[\\n,]")), Qt::SkipEmptyParts)) {
			item = item.trimmed();
			if (!item.isEmpty() && !out.contains(item, Qt::CaseInsensitive))
				out << item;
		}
		return out;
	}

	void applyFilters()
	{
		chat::FilterRules rules;
		rules.hiddenUsers = splitList(hiddenUsers_->toPlainText());
		rules.hideCommands = hideCommands_->isChecked();
		rules.blockedWords = splitList(blockedWords_->toPlainText());
		ChatManager &m = ChatManager::instance();
		m.setFilters(rules);
		m.save();
	}

	void removeEntry(const QString &entry)
	{
		if (entry.isEmpty())
			return;
		entries_.removeIf([&entry](const chat::ChatEntry &e) { return e.entry == entry; });
		apply();
	}

	void apply()
	{
		ChatManager &m = ChatManager::instance();
		const QStringList errors =
			m.configure(entries_, always_->isChecked() ? ChatManager::Mode::Always : ChatManager::Mode::Auto);
		m.save();
		entries_ = m.chats();
		refreshList();
		if (!errors.isEmpty()) {
			error_->setText(errors.join("\n"));
			error_->show();
		}
	}

	// One row per configured chat, updated in place (focus and drags survive the frequent state
	// updates); rebuilt only when the list itself changes.
	struct Row {
		QListWidgetItem *item = nullptr;
		QWidget *widget = nullptr;
		QCheckBox *enabled = nullptr;
		QLabel *avatar = nullptr;
		QLabel *name = nullptr;
		QLabel *where = nullptr;
		QLabel *statusIcon = nullptr;
		QLabel *status = nullptr;
		QLabel *live = nullptr;
		QLabel *detail = nullptr;
		QToolButton *remove = nullptr;
	};

	static QLabel *plainLabel(QWidget *parent)
	{
		auto *label = new QLabel(parent);
		label->setTextFormat(Qt::PlainText);
		return label;
	}

	Row buildRow(const QString &entry)
	{
		Row r;
		r.item = new QListWidgetItem(list_);
		r.item->setData(Qt::UserRole, entry);
		r.item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
		r.widget = new QWidget(list_);
		auto *layout = new QHBoxLayout(r.widget);
		layout->setContentsMargins(4, 3, 2, 3);
		r.enabled = new QCheckBox(r.widget);
		r.avatar = new QLabel(r.widget);
		r.avatar->setFixedSize(28, 28);
		auto *text = new QVBoxLayout();
		text->setSpacing(0);
		r.name = plainLabel(r.widget);
		QFont bold = r.name->font();
		bold.setBold(true);
		r.name->setFont(bold);
		r.where = plainLabel(r.widget);
		r.where->setForegroundRole(QPalette::PlaceholderText);
		auto *statusRow = new QHBoxLayout();
		statusRow->setSpacing(4);
		r.statusIcon = new QLabel(r.widget);
		r.status = plainLabel(r.widget);
		r.live = plainLabel(r.widget);
		{
			QPalette p = r.live->palette();
			p.setColor(QPalette::WindowText, ui::alertColor(p.color(QPalette::Window)));
			r.live->setPalette(p);
		}
		statusRow->addWidget(r.statusIcon);
		statusRow->addWidget(r.status);
		statusRow->addWidget(r.live);
		statusRow->addStretch(1);
		r.detail = plainLabel(r.widget);
		r.detail->setWordWrap(true);
		r.detail->setPalette(r.live->palette());
		text->addWidget(r.name);
		text->addWidget(r.where);
		text->addLayout(statusRow);
		text->addWidget(r.detail);
		r.remove = new QToolButton(r.widget);
		const QIcon removeIcon = ui::themedIcon("close", palette().color(QPalette::ButtonText), 16, devicePixelRatioF());
		if (removeIcon.isNull())
			r.remove->setText(QString::fromUtf8("✕"));
		else
			r.remove->setIcon(removeIcon);
		r.remove->setAutoRaise(true);
		r.remove->setCursor(Qt::PointingHandCursor);
		layout->addWidget(r.enabled, 0, Qt::AlignVCenter);
		layout->addWidget(r.avatar, 0, Qt::AlignVCenter);
		layout->addLayout(text, 1);
		layout->addWidget(r.remove, 0, Qt::AlignVCenter);
		connect(r.enabled, &QCheckBox::toggled, this, [this, entry](bool on) { setChatEnabled(entry, on); });
		// Deferred: removing the chat rebuilds the list and deletes this button.
		connect(r.remove, &QToolButton::clicked, this, [this, entry] {
			QMetaObject::invokeMethod(this, [this, entry] { removeEntry(entry); }, Qt::QueuedConnection);
		});
		list_->setItemWidget(r.item, r.widget);
		return r;
	}

	void updateRow(Row &r, const chat::ChatEntry &chatEntry, const QJsonArray &sources)
	{
		chat::SourceInfo info;
		chat::EntryError code = chat::EntryError::None;
		QString arg;
		const bool valid = chat::parseEntry(chatEntry.entry, info, code, arg);
		QString status = "idle", detail;
		int live = -1, viewers = -1;
		for (const QJsonValue v : sources) {
			const QJsonObject s = v.toObject();
			if (valid && s.value("key").toString() == info.key) {
				status = s.value("status").toString();
				live = s.value("live").toInt();
				viewers = s.value("viewers").toInt(-1);
				detail = s.value("detail").toString();
			}
		}
		chat::Profile profile;
		if (valid && !chat::isDemo(info))
			ProfileCache::instance().get(info, profile);
		const QString name = !profile.name.isEmpty() ? profile.name : valid ? info.channel : chatEntry.entry;

		{
			const QSignalBlocker block(r.enabled);
			r.enabled->setChecked(chatEntry.enabled);
		}
		r.enabled->setAccessibleName(ui::T("Settings.Row.Enabled").arg(name));
		r.enabled->setToolTip(ui::T("Settings.Row.EnabledTip"));
		r.avatar->setPixmap(valid ? ui::chatAvatar(info, 28, devicePixelRatioF()) : QPixmap());
		r.name->setText(name);
		r.where->setText(valid ? chat::platformName(info.platform) + QStringLiteral(" · ") + info.channel
				       : chatEntry.entry);
		r.widget->setEnabled(true);

		QString statusText;
		if (!valid) {
			status = "error";
			statusText = ui::T("Status.error");
			detail = ui::entryErrorText(code, arg);
		} else if (!chatEntry.enabled) {
			status = "idle";
			statusText = ui::T("Settings.Row.Paused");
			detail.clear();
			live = -1;
			viewers = -1;
		} else {
			statusText = ui::T(("Status." + status).toUtf8().constData());
		}
		r.statusIcon->setPixmap(
			ui::statusIcon(valid ? info.platform : QString(), status, 14, devicePixelRatioF()).pixmap(14, 14));
		r.status->setText(statusText);
		QString liveText;
		if (live == 1)
			liveText = QString::fromUtf8("· ● ") + ui::T("Live.Yes");
		if (viewers >= 0)
			liveText += (liveText.isEmpty() ? QStringLiteral("· ") : QStringLiteral("  ")) +
				    ui::T("Dock.Viewers").arg(ui::formatCount(viewers));
		r.live->setText(liveText);
		r.live->setVisible(!liveText.isEmpty());
		r.detail->setText(status == "error" ? detail : QString());
		r.detail->setVisible(status == "error" && !detail.isEmpty());
		r.widget->setToolTip(detail);
		// Paused rows look dimmed, but the switch and the remove button keep working.
		const QPalette::ColorRole nameRole = chatEntry.enabled ? QPalette::WindowText : QPalette::PlaceholderText;
		r.name->setForegroundRole(nameRole);
		r.status->setForegroundRole(nameRole);
		r.remove->setToolTip(ui::T("Settings.Row.Remove").arg(name));
		r.remove->setAccessibleName(ui::T("Settings.Row.Remove").arg(name));
		r.item->setSizeHint(r.widget->sizeHint());
		const QString accessible = name + ", " + r.where->text() + ", " + statusText;
		r.item->setData(Qt::AccessibleTextRole, accessible);
	}

	void refreshList(bool rebuild = false)
	{
		QStringList order;
		for (const chat::ChatEntry &e : entries_)
			order << e.entry;
		if (rebuild || !built_ || order != rowOrder_) {
			const QString selected = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString()
								      : QString();
			const bool hadFocus = list_->hasFocus() || list_->isAncestorOf(QApplication::focusWidget());
			rows_.clear();
			list_->clear();
			for (const QString &entry : order) {
				if (rows_.contains(entry))
					continue; // the same text twice (hand-edited config): one row
				rows_.insert(entry, buildRow(entry));
				if (entry == selected)
					list_->setCurrentItem(rows_[entry].item);
			}
			if (order.isEmpty()) {
				auto *item = new QListWidgetItem(ui::T("Settings.NoChats"), list_);
				item->setFlags(Qt::NoItemFlags);
			}
			rowOrder_ = order;
			built_ = true;
			if (hadFocus)
				list_->setFocus();
		}
		const QJsonArray sources = ChatManager::instance().state().value("sources").toArray();
		for (const chat::ChatEntry &e : entries_)
			updateRow(rows_[e.entry], e, sources);
	}

	void setChatEnabled(const QString &entry, bool on)
	{
		for (chat::ChatEntry &e : entries_)
			if (e.entry == entry)
				e.enabled = on;
		apply();
	}

	// Alt+Up / Alt+Down: the keyboard alternative to dragging.
	void moveCurrent(int delta)
	{
		QListWidgetItem *item = list_->currentItem();
		if (!item)
			return;
		const QString entry = item->data(Qt::UserRole).toString();
		qsizetype from = -1;
		for (qsizetype i = 0; i < entries_.size(); ++i)
			if (entries_[i].entry == entry)
				from = i;
		const qsizetype to = from + delta;
		if (from < 0 || to < 0 || to >= entries_.size())
			return;
		entries_.move(from, to);
		apply();
		if (rows_.contains(entry))
			list_->setCurrentItem(rows_[entry].item);
		list_->setFocus();
	}

	// After a drag: the list's order becomes the chats' order.
	void syncOrderFromList()
	{
		QList<chat::ChatEntry> ordered;
		for (int i = 0; i < list_->count(); ++i) {
			const QString entry = list_->item(i)->data(Qt::UserRole).toString();
			for (const chat::ChatEntry &e : entries_)
				if (e.entry == entry)
					ordered << e;
		}
		if (ordered.size() != entries_.size())
			return;
		const QString current = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString()
							     : QString();
		entries_ = ordered;
		apply();
		// Item widgets do not always follow a moved item: rebuild them.
		refreshList(true);
		if (rows_.contains(current))
			list_->setCurrentItem(rows_[current].item);
	}

	QList<chat::ChatEntry> entries_;
	ChannelInput *input_ = nullptr;
	ChatList *list_ = nullptr;
	QHash<QString, Row> rows_; // by entry
	QStringList rowOrder_;
	bool built_ = false;
	QLabel *error_ = nullptr;
	QTimer refreshTimer_;
	QRadioButton *auto_ = nullptr;
	QRadioButton *always_ = nullptr;
	QCheckBox *demo_ = nullptr;
	QPlainTextEdit *hiddenUsers_ = nullptr;
	QCheckBox *hideCommands_ = nullptr;
	QPlainTextEdit *blockedWords_ = nullptr;
	QTimer filtersTimer_;
};

} // namespace

void open_multichat_settings(QWidget *parent)
{
	static QPointer<SettingsDialog> dialog;
	if (!dialog)
		dialog = new SettingsDialog(parent ? parent->window() : nullptr);
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}
