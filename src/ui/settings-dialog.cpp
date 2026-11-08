// "Multichat" window: add, pause, reorder and remove chats, and choose when to connect them.

#include "settings-dialog.hpp"
#include "channel-input.hpp"
#include "chat-list.hpp"
#include "chat-dock.hpp"
#include "first-run-wizard.hpp"
#include "flow-layout.hpp"
#include "image-cache.hpp"
#include "profile-cache.hpp"
#include "ui-common.hpp"
#include "../i18n.hpp"
#include "../chat-manager.hpp"
#include "../chat/message.hpp"
#include "../updater.hpp"
#include "../platforms-registry.hpp"
#include "../tts/speech-backend.hpp"
#include "../tts/voice-source.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
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
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>
#include <memory>
#include <vector>

namespace {

class SettingsDialog : public QDialog {
public:
	explicit SettingsDialog(QWidget *parent) : QDialog(parent)
	{
		setWindowTitle(i18n::tr("Settings.Title"));
		setAttribute(Qt::WA_DeleteOnClose);
		setMinimumWidth(460);
		auto *root = new QVBoxLayout(this);
		auto *tabs = new QTabWidget(this);
		auto *chatsPage = new QWidget(tabs);
		auto *chatsPageLayout = new QVBoxLayout(chatsPage);
		tabs->addTab(chatsPage, i18n::tr("Settings.Tab.Chats"));
		tabs->addTab(buildFilters(tabs), i18n::tr("Settings.Tab.Filters"));
		tabs->addTab(buildVoice(tabs), i18n::tr("Settings.Tab.Voice"));
		root->addWidget(tabs);

		// Lista de chats.
		auto *chatsBox = new QGroupBox(i18n::tr("Settings.Chats"), chatsPage);
		auto *chatsLayout = new QVBoxLayout(chatsBox);
		list_ = new ChatList(chatsBox);
		list_->setMinimumHeight(160);
		list_->setDragDropMode(QAbstractItemView::InternalMove);
		list_->setDefaultDropAction(Qt::MoveAction);
		list_->setSelectionMode(QAbstractItemView::SingleSelection);
		list_->setAccessibleName(i18n::tr("Settings.Chats"));
		list_->onDropped = [this] {
			QMetaObject::invokeMethod(this, [this] { syncOrderFromList(); }, Qt::QueuedConnection);
		};
		chatsLayout->addWidget(list_);
		auto *listHelp = new QLabel(i18n::tr("Settings.ListHelp"), chatsBox);
		listHelp->setWordWrap(true);
		listHelp->setForegroundRole(QPalette::PlaceholderText);
		chatsLayout->addWidget(listHelp);

		input_ = new ChannelInput(chatsBox);
		input_->existing = [] { return ChatManager::instance().chats(); };
		input_->onAdd = [this](const QString &entry) {
			error_->hide();
			syncEntries();
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
		auto *modeBox = new QGroupBox(i18n::tr("Settings.When"), chatsPage);
		auto *modeLayout = new QVBoxLayout(modeBox);
		auto_ = new QRadioButton(i18n::tr("Settings.Mode.Auto"), modeBox);
		always_ = new QRadioButton(i18n::tr("Settings.Mode.Always"), modeBox);
		auto *autoHelp = new QLabel(i18n::tr("Settings.Mode.AutoHelp"), modeBox);
		autoHelp->setWordWrap(true);
		autoHelp->setForegroundRole(QPalette::PlaceholderText);
		autoHelp->setContentsMargins(22, 0, 0, 0);
		modeLayout->addWidget(auto_);
		modeLayout->addWidget(autoHelp);
		modeLayout->addWidget(always_);
		demo_ = new QCheckBox(i18n::tr("Settings.Demo"), modeBox);
		modeLayout->addWidget(demo_);
		chatsPageLayout->addWidget(modeBox);
		chatsPageLayout->addStretch(1);

		// Shortcuts that used to be Tools menu entries or a text telling where to click.
		auto *actions = new QHBoxLayout();
		auto *addSources = new QPushButton(i18n::tr("Settings.AddSources"), this);
		addSources->setToolTip(i18n::tr("Settings.AddSources.Help"));
		auto *wizard = new QPushButton(i18n::tr("Settings.Wizard"), this);
		auto *updates = new QPushButton(i18n::tr("Settings.CheckUpdates"), this);
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
			addResult->setText(ui::addMultichatSources(true, true).join('\n'));
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
		connect(auto_, &QRadioButton::toggled, this, [this] {
			syncEntries();
			apply();
		});
		connect(demo_, &QCheckBox::toggled, this, [](bool on) { ChatManager::instance().setTestMessages(on); });
		connect(&m, &ChatManager::testMessagesChanged, this, [this](bool on) {
			if (demo_->isChecked() != on)
				demo_->setChecked(on);
		});
		// Chats added elsewhere (the assistant) while this is open: keep them, or the next change
		// made here would write the old list back.
		connect(&m, &ChatManager::stateChanged, this, [this] {
			syncEntries();
			refreshList();
		});

		refreshList();
	}

	~SettingsDialog() override
	{
		// Closed right after an edit: do not lose it.
		if (filtersTimer_.isActive())
			applyFilters();
		if (voiceTimer_.isActive())
			applyVoice();
	}

private:
	// "Filters" tab: hidden users, commands and blocked words. Saved a moment after each change.
	QWidget *buildFilters(QWidget *parent)
	{
		auto *page = new QWidget(parent);
		auto *layout = new QVBoxLayout(page);
		const chat::FilterRules rules = ChatManager::instance().filters();
		auto help = [page](const char *key) {
			auto *label = new QLabel(i18n::tr(key), page);
			label->setWordWrap(true);
			label->setForegroundRole(QPalette::PlaceholderText);
			return label;
		};
		auto listEdit = [page](const QStringList &items, const char *placeholder) {
			auto *edit = new QPlainTextEdit(page);
			edit->setPlainText(items.join('\n'));
			edit->setPlaceholderText(i18n::tr(placeholder));
			edit->setTabChangesFocus(true);
			edit->setMinimumHeight(90);
			return edit;
		};

		auto *usersBox = new QGroupBox(i18n::tr("Settings.Filters.Users"), page);
		auto *usersLayout = new QVBoxLayout(usersBox);
		hiddenUsers_ = listEdit(rules.hiddenUsers, "Settings.Filters.UsersPlaceholder");
		usersLayout->addWidget(hiddenUsers_);
		auto *usersRow = new QHBoxLayout();
		usersRow->addWidget(help("Settings.Filters.UsersHelp"), 1);
		auto *restore = new QPushButton(i18n::tr("Settings.Filters.RestoreBots"), usersBox);
		usersRow->addWidget(restore, 0, Qt::AlignTop);
		usersLayout->addLayout(usersRow);
		layout->addWidget(usersBox);

		auto *commandsBox = new QGroupBox(i18n::tr("Settings.Filters.Commands"), page);
		auto *commandsLayout = new QVBoxLayout(commandsBox);
		hideCommands_ = new QCheckBox(i18n::tr("Settings.Filters.HideCommands"), commandsBox);
		hideCommands_->setChecked(rules.hideCommands);
		commandsLayout->addWidget(hideCommands_);
		commandsLayout->addWidget(help("Settings.Filters.CommandsHelp"));
		layout->addWidget(commandsBox);

		auto *wordsBox = new QGroupBox(i18n::tr("Settings.Filters.Words"), page);
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
		// A user hidden from the dock while this is open: add only that one here so a later edit
		// keeps it. The rest of the text stays as typed, so a user just removed here (its change
		// still pending) does not come back.
		connect(&ChatManager::instance(), &ChatManager::filtersChanged, this, [this](const QString &user) {
			QStringList users = splitList(hiddenUsers_->toPlainText());
			if (user.isEmpty() || users.contains(user, Qt::CaseInsensitive))
				return;
			users << user;
			const QSignalBlocker block(hiddenUsers_);
			hiddenUsers_->setPlainText(users.join('\n'));
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

	// "Voice" tab: the plugin's voice output reads the chat aloud in every scene. Saved a moment
	// after each change; blocked words and hidden users stay in the Filters tab, volume and
	// monitoring in OBS's audio mixer.
	QWidget *buildVoice(QWidget *parent)
	{
		auto *page = new QWidget(parent);
		auto *layout = new QVBoxLayout(page);
		auto help = [page](const char *key) {
			auto *label = new QLabel(i18n::tr(key), page);
			label->setWordWrap(true);
			label->setForegroundRole(QPalette::PlaceholderText);
			return label;
		};

		voiceEnabled_ = new QCheckBox(i18n::tr("Voice.Enabled"), page);
		QFont bold = voiceEnabled_->font();
		bold.setBold(true);
		voiceEnabled_->setFont(bold);
		layout->addWidget(voiceEnabled_);
		layout->addWidget(help("Voice.Enabled.Help"));

		auto *form = new QFormLayout();
		form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		voiceMode_ = new QComboBox(page);
		for (const char *id : {"read", "sound", "both"})
			voiceMode_->addItem(i18n::tr(("Voice.Mode." + QString::fromLatin1(id)).toUtf8().constData()),
					    QString::fromLatin1(id));
		voiceMode_->setToolTip(i18n::tr("Voice.Mode.Help"));
		form->addRow(i18n::tr("Voice.Mode"), voiceMode_);

		// Sound options.
		auto *soundRow = new QWidget(page);
		auto *soundLayout = new QHBoxLayout(soundRow);
		soundLayout->setContentsMargins(0, 0, 0, 0);
		soundFile_ = new QLineEdit(soundRow);
		soundFile_->setPlaceholderText(i18n::tr("Voice.SoundFile.Placeholder"));
		soundFile_->setToolTip(i18n::tr("Voice.SoundFile.Help"));
		auto *browse = new QPushButton(i18n::tr("Voice.Browse"), soundRow);
		browse->setAutoDefault(false);
		soundLayout->addWidget(soundFile_, 1);
		soundLayout->addWidget(browse);
		form->addRow(i18n::tr("Voice.SoundFile"), soundRow);
		soundCooldown_ = new QSpinBox(page);
		soundCooldown_->setRange(0, tts::VoiceSettings::kMaxCooldownMs);
		soundCooldown_->setSingleStep(100);
		soundCooldown_->setSuffix(QStringLiteral(" ms"));
		soundCooldown_->setToolTip(i18n::tr("Voice.SoundCooldown.Help"));
		form->addRow(i18n::tr("Voice.SoundCooldown"), soundCooldown_);
		soundWidgets_ = {soundRow, soundCooldown_};

		// Reading options.
		voice_ = new QComboBox(page);
		voice_->addItem(i18n::tr("Voice.DefaultVoice"), QString());
		for (const tts::VoiceInfo &info : tts::availableVoices())
			voice_->addItem(info.name, info.id);
		form->addRow(i18n::tr("Voice.Voice"), voice_);
		auto *rateRow = new QWidget(page);
		auto *rateLayout = new QHBoxLayout(rateRow);
		rateLayout->setContentsMargins(0, 0, 0, 0);
		rate_ = new QSlider(Qt::Horizontal, rateRow);
		rate_->setRange(tts::VoiceSettings::kMinRate, tts::VoiceSettings::kMaxRate);
		rate_->setToolTip(i18n::tr("Voice.Rate.Help"));
		rateValue_ = new QLabel(rateRow);
		rateValue_->setMinimumWidth(rateValue_->fontMetrics().horizontalAdvance(QStringLiteral("100 %")));
		rateLayout->addWidget(rate_, 1);
		rateLayout->addWidget(rateValue_);
		form->addRow(i18n::tr("Voice.Rate"), rateRow);
		maxLength_ = new QSpinBox(page);
		maxLength_->setRange(tts::VoiceSettings::kMinMaxLength, tts::VoiceSettings::kMaxMaxLength);
		maxLength_->setSingleStep(10);
		maxLength_->setToolTip(i18n::tr("Voice.MaxLength.Help"));
		form->addRow(i18n::tr("Voice.MaxLength"), maxLength_);
		layout->addLayout(form);
		readAuthor_ = new QCheckBox(i18n::tr("Voice.ReadAuthor"), page);
		layout->addWidget(readAuthor_);
		readWidgets_ = {voice_, rateRow, maxLength_, readAuthor_};
		for (QWidget *w : {static_cast<QWidget *>(soundRow), static_cast<QWidget *>(soundCooldown_),
				   static_cast<QWidget *>(voice_), rateRow, static_cast<QWidget *>(maxLength_)})
			if (QWidget *label = form->labelForField(w)) {
				(soundWidgets_.contains(w) ? soundWidgets_ : readWidgets_) << label;
			}

		// Platforms: the filters apply to every mode.
		auto *platformsBox = new QWidget(page);
		auto *platformsRow = new FlowLayout(platformsBox, 12);
		platformsRow->setContentsMargins(0, 0, 0, 0);
		const QString readPlatform = i18n::tr("Voice.ReadPlatform");
		for (const platforms::Platform &platform : platforms::kAll) {
			const QString id = QString::fromUtf8(platform.id);
			auto *box = new QCheckBox(readPlatform.arg(chat::platformName(id)), platformsBox);
			readPlatforms_.insert(id, box);
			platformsRow->addWidget(box);
		}
		layout->addWidget(platformsBox);

		auto *skipRow = new QHBoxLayout();
		auto *skip = new QPushButton(i18n::tr("Voice.Skip"), page);
		skip->setAutoDefault(false);
		skipRow->addWidget(skip);
		skipRow->addStretch(1);
		layout->addLayout(skipRow);
		readWidgets_ << skip;
		layout->addWidget(help("Voice.Note"));
		layout->addStretch(1);

		loadVoice();

		voiceTimer_.setSingleShot(true);
		voiceTimer_.setInterval(400);
		connect(&voiceTimer_, &QTimer::timeout, this, [this] { applyVoice(); });
		auto changed = [this] { voiceTimer_.start(); };
		// Turning the voice on or off is not delayed: it creates or removes the mixer entry.
		connect(voiceEnabled_, &QCheckBox::toggled, this, [this] { applyVoice(); });
		connect(voiceMode_, &QComboBox::currentIndexChanged, this, [this, changed] {
			updateVoiceVisibility();
			changed();
		});
		connect(soundFile_, &QLineEdit::textChanged, this, changed);
		connect(soundCooldown_, &QSpinBox::valueChanged, this, changed);
		connect(voice_, &QComboBox::currentIndexChanged, this, changed);
		connect(rate_, &QSlider::valueChanged, this, [this, changed](int value) {
			rateValue_->setText(QStringLiteral("%1 %").arg(value));
			changed();
		});
		connect(maxLength_, &QSpinBox::valueChanged, this, changed);
		connect(readAuthor_, &QCheckBox::toggled, this, changed);
		for (QCheckBox *box : std::as_const(readPlatforms_))
			connect(box, &QCheckBox::toggled, this, changed);
		connect(browse, &QPushButton::clicked, this, [this] {
			const QString file = QFileDialog::getOpenFileName(this, i18n::tr("Voice.SoundFile"),
									  soundFile_->text(), i18n::tr("Voice.SoundFilter"));
			if (!file.isEmpty())
				soundFile_->setText(file);
		});
		connect(skip, &QPushButton::clicked, this, [] { voice_skip_current(); });
		// Turned on or off elsewhere (the assistant) while this is open.
		connect(&ChatManager::instance(), &ChatManager::voiceSettingsChanged, this, [this] {
			if (!voiceTimer_.isActive())
				loadVoice();
		});
		return page;
	}

	// The widgets show the saved voice settings (without counting as changes).
	void loadVoice()
	{
		const tts::VoiceSettings v = ChatManager::instance().voiceSettings();
		QList<QObject *> widgets = {voiceEnabled_, voiceMode_, soundFile_, soundCooldown_,
					    voice_,        rate_,      maxLength_, readAuthor_};
		for (QCheckBox *box : std::as_const(readPlatforms_))
			widgets << box;
		std::vector<std::unique_ptr<QSignalBlocker>> blockers;
		for (QObject *w : widgets)
			blockers.push_back(std::make_unique<QSignalBlocker>(w));
		voiceEnabled_->setChecked(v.enabled);
		voiceMode_->setCurrentIndex(std::max(0, voiceMode_->findData(tts::voiceModeToString(v.mode))));
		soundFile_->setText(v.soundFile);
		soundCooldown_->setValue(v.soundCooldownMs);
		int voiceIndex = voice_->findData(v.voice);
		if (voiceIndex < 0) { // a voice this system does not have (any more): kept as it is
			voice_->addItem(v.voice, v.voice);
			voiceIndex = voice_->count() - 1;
		}
		voice_->setCurrentIndex(voiceIndex);
		rate_->setValue(v.ratePercent);
		rateValue_->setText(QStringLiteral("%1 %").arg(v.ratePercent));
		maxLength_->setValue(v.maxLength);
		readAuthor_->setChecked(v.readAuthor);
		for (auto it = readPlatforms_.cbegin(); it != readPlatforms_.cend(); ++it)
			it.value()->setChecked(v.readsPlatform(it.key()));
		updateVoiceVisibility();
	}

	// The sound options only show when a sound is used; the reading ones, when the chat is read.
	void updateVoiceVisibility()
	{
		const tts::VoiceMode mode = tts::voiceModeFromString(voiceMode_->currentData().toString());
		for (QWidget *w : std::as_const(soundWidgets_))
			w->setVisible(mode != tts::VoiceMode::Read);
		for (QWidget *w : std::as_const(readWidgets_))
			w->setVisible(mode != tts::VoiceMode::Sound);
	}

	void applyVoice()
	{
		voiceTimer_.stop();
		tts::VoiceSettings v;
		v.enabled = voiceEnabled_->isChecked();
		v.mode = tts::voiceModeFromString(voiceMode_->currentData().toString());
		v.voice = voice_->currentData().toString();
		v.ratePercent = rate_->value();
		v.readAuthor = readAuthor_->isChecked();
		for (const platforms::Platform &platform : platforms::kAll) {
			const QString id = QString::fromUtf8(platform.id);
			if (QCheckBox *box = readPlatforms_.value(id); box && !box->isChecked())
				v.mutedPlatforms << id;
		}
		v.maxLength = maxLength_->value();
		v.soundFile = soundFile_->text().trimmed();
		v.soundCooldownMs = soundCooldown_->value();
		ChatManager::instance().setVoiceSettings(v);
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

	// The chats as ChatManager has them now: every change made here starts from that list.
	void syncEntries() { entries_ = ChatManager::instance().chats(); }

	void removeEntry(const QString &entry)
	{
		if (entry.isEmpty())
			return;
		syncEntries();
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
		r.enabled->setAccessibleName(i18n::tr("Settings.Row.Enabled").arg(name));
		r.enabled->setToolTip(i18n::tr("Settings.Row.EnabledTip"));
		r.avatar->setPixmap(valid ? ui::chatAvatar(info, 28, devicePixelRatioF()) : QPixmap());
		r.name->setText(name);
		r.where->setText(valid ? chat::platformName(info.platform) + QStringLiteral(" · ") + info.channel
				       : chatEntry.entry);
		r.widget->setEnabled(true);

		QString statusText;
		if (!valid) {
			status = "error";
			statusText = i18n::tr("Status.error");
			detail = ui::entryErrorText(code, arg);
		} else if (!chatEntry.enabled) {
			status = "idle";
			statusText = i18n::tr("Settings.Row.Paused");
			detail.clear();
			live = -1;
			viewers = -1;
		} else {
			statusText = i18n::tr(("Status." + status).toUtf8().constData());
		}
		r.statusIcon->setPixmap(
			ui::statusIcon(valid ? info.platform : QString(), status, 14, devicePixelRatioF()).pixmap(14, 14));
		r.status->setText(statusText);
		QString liveText;
		if (live == 1)
			liveText = QString::fromUtf8("· ● ") + i18n::tr("Live.Yes");
		if (viewers >= 0)
			liveText += (liveText.isEmpty() ? QStringLiteral("· ") : QStringLiteral("  ")) +
				    i18n::tr("Dock.Viewers").arg(ui::formatCount(viewers));
		r.live->setText(liveText);
		r.live->setVisible(!liveText.isEmpty());
		r.detail->setText(status == "error" ? detail : QString());
		r.detail->setVisible(status == "error" && !detail.isEmpty());
		r.widget->setToolTip(detail);
		// Paused rows look dimmed, but the switch and the remove button keep working.
		const QPalette::ColorRole nameRole = chatEntry.enabled ? QPalette::WindowText : QPalette::PlaceholderText;
		r.name->setForegroundRole(nameRole);
		r.status->setForegroundRole(nameRole);
		r.remove->setToolTip(i18n::tr("Settings.Row.Remove").arg(name));
		r.remove->setAccessibleName(i18n::tr("Settings.Row.Remove").arg(name));
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
				auto *item = new QListWidgetItem(i18n::tr("Settings.NoChats"), list_);
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
		syncEntries();
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
		syncEntries();
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
		syncEntries();
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
	QCheckBox *voiceEnabled_ = nullptr;
	QComboBox *voiceMode_ = nullptr;
	QLineEdit *soundFile_ = nullptr;
	QSpinBox *soundCooldown_ = nullptr;
	QComboBox *voice_ = nullptr;
	QSlider *rate_ = nullptr;
	QLabel *rateValue_ = nullptr;
	QCheckBox *readAuthor_ = nullptr;
	QHash<QString, QCheckBox *> readPlatforms_; // by platform id
	QSpinBox *maxLength_ = nullptr;
	QList<QWidget *> soundWidgets_; // shown when the mode plays a sound
	QList<QWidget *> readWidgets_;  // shown when the mode reads aloud
	QTimer voiceTimer_;
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
