// Ventana "Multichat": añadir y quitar chats, y elegir cuándo conectarlos.

#include "chat-dock.hpp"
#include "image-cache.hpp"
#include "profile-cache.hpp"
#include "ui-common.hpp"
#include "../chat-manager.hpp"
#include "../chat/message.hpp"

#include <QButtonGroup>
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

namespace {

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
		list_ = new QListWidget(chatsBox);
		list_->setIconSize(QSize(18, 18));
		list_->setMinimumHeight(140);
		chatsLayout->addWidget(list_);

		auto *addRow = new QHBoxLayout();
		platform_ = new QComboBox(chatsBox);
		for (const char *p : {"twitch", "youtube", "kick", "tiktok", "starvios"})
			platform_->addItem(QIcon(ui::platformIcon(p, 16, devicePixelRatioF())), chat::platformName(p),
					   QString::fromLatin1(p));
		channel_ = new QLineEdit(chatsBox);
		channel_->setAccessibleName(ui::T("Settings.Chats"));
		add_ = new QPushButton(ui::T("Settings.Add"), chatsBox);
		add_->setAutoDefault(false);
		addRow->addWidget(platform_);
		addRow->addWidget(channel_, 1);
		addRow->addWidget(add_);
		chatsLayout->addLayout(addRow);

		// Vista previa del canal escrito o pegado: foto de perfil y nombre.
		preview_ = new QWidget(chatsBox);
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
		chatsLayout->addWidget(preview_);
		// What is wrong with the typed channel (format, repeated, not found), right under it.
		check_ = new QLabel(chatsBox);
		check_->setWordWrap(true);
		check_->setTextFormat(Qt::PlainText);
		{
			QPalette p = check_->palette();
			p.setColor(QPalette::WindowText, ui::alertColor(p.color(QPalette::Window)));
			check_->setPalette(p);
		}
		check_->hide();
		chatsLayout->addWidget(check_);
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
		// Perfiles y fotos llegan en segundo plano (y el dock pide emotes a la misma caché):
		// se agrupan en una sola actualización.
		refreshTimer_.setSingleShot(true);
		refreshTimer_.setInterval(200);
		connect(&refreshTimer_, &QTimer::timeout, this, [this] {
			updatePreview();
			refreshList();
		});
		connect(&ProfileCache::instance(), &ProfileCache::ready, this, [this] { refreshTimer_.start(); });
		connect(&ImageCache::instance(), &ImageCache::ready, this, [this] { refreshTimer_.start(); });

		auto *hintRow = new QHBoxLayout();
		hint_ = new QLabel(chatsBox);
		hint_->setWordWrap(true);
		hint_->setForegroundRole(QPalette::PlaceholderText);
		hintRow->addWidget(hint_, 1);
		chatsLayout->addLayout(hintRow);
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

		auto *overlayHelp = new QLabel(ui::T("Settings.OverlayHelp"), this);
		overlayHelp->setWordWrap(true);
		root->addWidget(overlayHelp);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
		root->addWidget(buttons);

		ChatManager &m = ChatManager::instance();
		entries_ = m.chats();
		(m.mode() == ChatManager::Mode::Always ? always_ : auto_)->setChecked(true);
		demo_->setChecked(m.demo());

		connect(platform_, &QComboBox::currentIndexChanged, this, [this] {
			updateHint();
			previewTimer_.start();
		});
		// The button confirms a channel that was not found; Enter alone never adds it silently.
		connect(add_, &QPushButton::clicked, this, [this] { addEntry(!notFound_.isEmpty() && notFound_ == entryFromInput()); });
		connect(channel_, &QLineEdit::returnPressed, this, [this] { addEntry(false); });
		auto *removeKey = new QShortcut(QKeySequence::Delete, list_);
		connect(removeKey, &QShortcut::activated, this, [this] {
			if (QListWidgetItem *item = list_->currentItem())
				removeEntry(item->data(Qt::UserRole).toString());
		});
		connect(auto_, &QRadioButton::toggled, this, [this] { apply(); });
		connect(demo_, &QCheckBox::toggled, this, [this] { apply(); });
		connect(&m, &ChatManager::stateChanged, this, [this] { refreshList(); });

		updateHint();
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

	void updateHint()
	{
		const QString p = platform_->currentData().toString();
		channel_->setPlaceholderText(ui::T(("Settings.Placeholder." + p).toUtf8().constData()));
		hint_->setText(ui::T(("Settings.Hint." + p).toUtf8().constData()));
	}

	// A pasted URL or "platform:channel" is kept; anything else uses the platform picked in the list.
	QString entryFromInput() const
	{
		return chat::entryFromInput(channel_->text(), platform_->currentData().toString());
	}

	static QString errorText(chat::EntryError code, const QString &arg)
	{
		const char *key = chat::entryErrorKey(code);
		return key ? ui::T(key).arg(arg) : QString();
	}

	// Foto de perfil del canal (o el logo de la plataforma mientras llega o si no hay).
	QPixmap avatarFor(const chat::SourceInfo &info, int size)
	{
		chat::Profile profile;
		QImage photo;
		if (ProfileCache::instance().get(info, profile) && !profile.avatar.isEmpty())
			photo = ImageCache::instance().get(profile.avatar);
		return ui::avatarPixmap(photo, info.platform, size, devicePixelRatioF());
	}

	// What is known about the typed channel.
	enum class Check { Empty, Invalid, Duplicate, Loading, Found, NotFound };
	struct Validation {
		Check state = Check::Empty;
		QString entry;
		chat::SourceInfo info;
		chat::Profile profile;
		QString message; // why it cannot be added (Invalid, Duplicate)
	};

	Validation validate()
	{
		Validation v;
		v.entry = entryFromInput();
		if (v.entry.isEmpty())
			return v;
		chat::EntryError code = chat::EntryError::None;
		QString arg;
		if (!chat::parseEntry(v.entry, v.info, code, arg)) {
			v.state = Check::Invalid;
			v.message = errorText(code, arg);
			return v;
		}
		for (const chat::ChatEntry &e : entries_) {
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
		else if (v.profile.name.isEmpty() && v.profile.avatar.isEmpty())
			v.state = Check::NotFound;
		else
			v.state = Check::Found;
		return v;
	}

	void updatePreview()
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
			avatar_->setPixmap(avatarFor(v.info, 40));
			busy_->setVisible(v.state == Check::Loading);
			preview_->show();
		}

		// A pending add finishes when the lookup answers.
		const bool pending = !pendingAdd_.isEmpty() && pendingAdd_ == v.entry;
		if (pending && v.state == Check::Found) {
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
	void addEntry(bool confirmed)
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
			commit(v.entry);
			return;
		}
	}

	void commit(const QString &entry)
	{
		pendingAdd_.clear();
		notFound_.clear();
		error_->hide();
		check_->hide();
		preview_->hide();
		{
			const QSignalBlocker block(channel_);
			channel_->clear();
		}
		add_->setText(ui::T("Settings.Add"));
		add_->setEnabled(true);
		entries_ << chat::ChatEntry{entry, true};
		apply();
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
		const QStringList errors = m.configure(
			entries_, always_->isChecked() ? ChatManager::Mode::Always : ChatManager::Mode::Auto,
			demo_->isChecked());
		m.save();
		entries_ = m.chats();
		refreshList();
		if (!errors.isEmpty()) {
			error_->setText(errors.join("\n"));
			error_->show();
		}
	}

	void refreshList()
	{
		const QJsonArray sources = ChatManager::instance().state().value("sources").toArray();
		const QString selected = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString()
							      : QString();
		list_->clear();
		for (const chat::ChatEntry &chatEntry : entries_) {
			const QString &entry = chatEntry.entry;
			chat::SourceInfo info;
			QString error;
			if (!chat::parseEntry(entry, info, error))
				continue;
			QString status = "idle";
			int live = -1, viewers = -1;
			QString detail;
			for (const QJsonValue v : sources) {
				const QJsonObject s = v.toObject();
				if (s.value("key").toString() == info.key) {
					status = s.value("status").toString();
					live = s.value("live").toInt();
					viewers = s.value("viewers").toInt(-1);
					detail = s.value("detail").toString();
				}
			}
			QString text = chat::platformName(info.platform) + QStringLiteral(" · ") + info.channel +
				       "   —   " + ui::T(("Status." + status).toUtf8().constData());
			if (live == 1)
				text += QString::fromUtf8("  🔴 ") + ui::T("Live.Yes");
			if (viewers >= 0)
				text += QString::fromUtf8("  👁 ") + ui::formatCount(viewers);
			if (status == "error" && !detail.isEmpty())
				text += "\n" + detail;
			// Fila: logo con el estado, texto y una ✕ a la derecha para quitar el canal. Los
			// clics en el texto llegan a la lista (QLabel no los usa), así que se puede seleccionar.
			auto *item = new QListWidgetItem(list_);
			item->setData(Qt::UserRole, entry);
			auto *row = new QWidget(list_);
			if (!detail.isEmpty())
				row->setToolTip(detail);
			auto *rowLayout = new QHBoxLayout(row);
			rowLayout->setContentsMargins(4, 2, 2, 2);
			auto *avatar = new QLabel(row);
			avatar->setPixmap(avatarFor(info, 28));
			auto *icon = new QLabel(row);
			icon->setPixmap(ui::statusIcon(info.platform, status, 18, devicePixelRatioF()).pixmap(18, 18));
			auto *label = new QLabel(text, row);
			label->setTextFormat(Qt::PlainText);
			label->setWordWrap(true);
			auto *remove = new QToolButton(row);
			remove->setText(QString::fromUtf8("✕"));
			remove->setToolTip(ui::T("Settings.Remove"));
			remove->setAutoRaise(true);
			remove->setCursor(Qt::PointingHandCursor);
			rowLayout->addWidget(avatar, 0, Qt::AlignTop);
			rowLayout->addWidget(icon, 0, Qt::AlignTop);
			rowLayout->addWidget(label, 1);
			rowLayout->addWidget(remove, 0, Qt::AlignVCenter);
			// Diferido: quitar el canal reconstruye la lista y borra este botón.
			connect(remove, &QToolButton::clicked, this, [this, entry] {
				QMetaObject::invokeMethod(this, [this, entry] { removeEntry(entry); }, Qt::QueuedConnection);
			});
			item->setSizeHint(row->sizeHint());
			list_->setItemWidget(item, row);
			if (entry == selected)
				list_->setCurrentItem(item);
		}
		if (entries_.isEmpty()) {
			auto *item = new QListWidgetItem(ui::T("Settings.NoChats"), list_);
			item->setFlags(Qt::NoItemFlags);
		}
	}

	QList<chat::ChatEntry> entries_;
	QListWidget *list_ = nullptr;
	QComboBox *platform_ = nullptr;
	QLineEdit *channel_ = nullptr;
	QLabel *hint_ = nullptr;
	QLabel *error_ = nullptr;
	QWidget *preview_ = nullptr;
	QLabel *avatar_ = nullptr;
	QLabel *previewText_ = nullptr;
	QProgressBar *busy_ = nullptr;
	QLabel *check_ = nullptr;
	QPushButton *add_ = nullptr;
	QString pendingAdd_; // entry to add once its lookup answers
	QString notFound_;   // entry shown as "not found": the button confirms it
	QTimer previewTimer_;
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
