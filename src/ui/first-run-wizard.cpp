#include "first-run-wizard.hpp"
#include "chat-dock.hpp"
#include "settings-dialog.hpp"
#include "channel-input.hpp"
#include "ui-common.hpp"
#include "../i18n.hpp"
#include "../chat-manager.hpp"
#include "../chat/platforms.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QLabel *wrapped(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	label->setWordWrap(true);
	label->setTextFormat(Qt::PlainText);
	return label;
}

QLabel *title(const QString &text, QWidget *parent)
{
	auto *label = wrapped(text, parent);
	QFont font = label->font();
	font.setBold(true);
	font.setPointSizeF(font.pointSizeF() * 1.35);
	label->setFont(font);
	label->setAccessibleName(text);
	return label;
}

class FirstRunWizard : public QDialog {
public:
	explicit FirstRunWizard(QWidget *parent) : QDialog(parent)
	{
		setWindowTitle(i18n::tr("Wizard.Title"));
		setAttribute(Qt::WA_DeleteOnClose);
		setMinimumWidth(500);
		auto *root = new QVBoxLayout(this);
		pages_ = new QStackedWidget(this);
		root->addWidget(pages_, 1);
		pages_->addWidget(welcomePage());
		pages_->addWidget(chatPage());
		pages_->addWidget(sourcesPage());
		pages_->addWidget(donePage());

		auto *buttons = new QHBoxLayout();
		skip_ = new QPushButton(i18n::tr("Wizard.Skip"), this);
		back_ = new QPushButton(i18n::tr("Wizard.Back"), this);
		next_ = new QPushButton(i18n::tr("Wizard.Next"), this);
		// Enter in the channel field adds the channel; it must not also turn the page.
		for (QPushButton *b : {skip_, back_, next_})
			b->setAutoDefault(false);
		buttons->addWidget(skip_);
		buttons->addStretch(1);
		buttons->addWidget(back_);
		buttons->addWidget(next_);
		root->addLayout(buttons);
		connect(skip_, &QPushButton::clicked, this, &QDialog::reject);
		connect(back_, &QPushButton::clicked, this, [this] { go(pages_->currentIndex() - 1); });
		connect(next_, &QPushButton::clicked, this, [this] {
			if (pages_->currentIndex() == pages_->count() - 1)
				accept();
			else
				go(pages_->currentIndex() + 1);
		});
		go(0);
	}

	void done(int result) override
	{
		// Finished, skipped or closed: it does not open by itself again.
		ChatManager::instance().setWizardShown();
		// Finished: the dock is where everything happens, so it must be on screen.
		if (result == QDialog::Accepted)
			show_multichat_dock();
		QDialog::done(result);
	}

private:
	void go(int index)
	{
		pages_->setCurrentIndex(index);
		const bool last = index == pages_->count() - 1;
		back_->setVisible(index > 0 && !last);
		skip_->setVisible(!last);
		next_->setText(i18n::tr(last ? "Wizard.Finish" : "Wizard.Next"));
		if (index == 1)
			input_->focusInput();
		else
			next_->setFocus();
		if (index == 2)
			updateScene();
	}

	QWidget *welcomePage()
	{
		auto *page = new QWidget(this);
		auto *layout = new QVBoxLayout(page);
		layout->addWidget(title(i18n::tr("Wizard.Welcome.Title"), page));
		layout->addWidget(wrapped(i18n::tr("Wizard.Welcome.Text"), page));
		layout->addStretch(1);
		return page;
	}

	QWidget *chatPage()
	{
		auto *page = new QWidget(this);
		auto *layout = new QVBoxLayout(page);
		layout->addWidget(title(i18n::tr("Wizard.Chat.Title"), page));
		layout->addWidget(wrapped(i18n::tr("Wizard.Chat.Text"), page));
		input_ = new ChannelInput(page);
		// One decision less: the platform follows the streaming service set in OBS.
		input_->setPlatform(chat::platformForService(ui::streamingServiceName()));
		input_->existing = [] { return ChatManager::instance().chats(); };
		input_->onAdd = [this](const QString &entry) {
			ChatManager &m = ChatManager::instance();
			QList<chat::ChatEntry> chats = m.chats();
			chats << chat::ChatEntry{entry, true};
			m.configure(chats, m.mode());
			m.save();
			updateAdded();
		};
		layout->addWidget(input_);
		added_ = wrapped(QString(), page);
		layout->addWidget(added_);
		auto *demo = new QCheckBox(i18n::tr("Wizard.Chat.Demo"), page);
		demo->setChecked(ChatManager::instance().testMessages());
		connect(demo, &QCheckBox::toggled, this, [](bool on) { ChatManager::instance().setTestMessages(on); });
		layout->addWidget(demo);
		layout->addWidget(wrapped(i18n::tr("Wizard.Chat.Later"), page));
		layout->addStretch(1);
		updateAdded();
		return page;
	}

	void updateAdded()
	{
		QStringList names;
		for (const chat::ChatEntry &e : ChatManager::instance().chats()) {
			chat::SourceInfo info;
			QString error;
			if (chat::parseEntry(e.entry, info, error))
				names << chat::platformName(info.platform) + QStringLiteral(" · ") + info.channel;
		}
		added_->setText(names.isEmpty() ? QString() : i18n::tr("Wizard.Chat.Added").arg(names.join(", ")));
		added_->setVisible(!names.isEmpty());
	}

	QWidget *sourcesPage()
	{
		auto *page = new QWidget(this);
		auto *layout = new QVBoxLayout(page);
		layout->addWidget(title(i18n::tr("Wizard.Sources.Title"), page));
		layout->addWidget(wrapped(i18n::tr("Wizard.Sources.Text"), page));
		// One button adds both sources; the boxes are there to leave one out.
		addSources_ = new QPushButton(page);
		addSources_->setAutoDefault(false);
		QFont bold = addSources_->font();
		bold.setBold(true);
		addSources_->setFont(bold);
		auto *row = new QHBoxLayout();
		row->addWidget(addSources_);
		row->addStretch(1);
		layout->addLayout(row);
		result_ = wrapped(QString(), page);
		result_->hide();
		layout->addWidget(result_);
		auto source = [page, layout](const char *id) {
			auto *box = new QCheckBox(QString::fromUtf8(obs_source_get_display_name(id)), page);
			box->setChecked(true);
			layout->addWidget(box);
			return box;
		};
		chatSource_ = source("multichat_overlay");
		featuredSource_ = source("multichat_featured");
		auto *featuredHelp = wrapped(i18n::tr("Wizard.Sources.FeaturedHelp"), page);
		featuredHelp->setContentsMargins(22, 0, 0, 0);
		layout->addWidget(featuredHelp);
		// The voice is not a source: this turns on the plugin's voice output (same switch as the
		// settings' Voice tab).
		layout->addSpacing(8);
		readAloud_ = new QCheckBox(i18n::tr("Voice.Enabled"), page);
		readAloud_->setChecked(ChatManager::instance().voiceSettings().enabled);
		layout->addWidget(readAloud_);
		auto *voiceHelp = wrapped(i18n::tr("Wizard.Sources.VoiceHelp"), page);
		voiceHelp->setContentsMargins(22, 0, 0, 0);
		layout->addWidget(voiceHelp);
		layout->addStretch(1);
		connect(addSources_, &QPushButton::clicked, this, [this] { addSources(); });
		auto enable = [this] { addSources_->setEnabled(chatSource_->isChecked() || featuredSource_->isChecked()); };
		for (QCheckBox *box : {chatSource_, featuredSource_})
			connect(box, &QCheckBox::toggled, this, enable);
		connect(readAloud_, &QCheckBox::toggled, this, [](bool on) {
			ChatManager &m = ChatManager::instance();
			tts::VoiceSettings settings = m.voiceSettings();
			settings.enabled = on;
			m.setVoiceSettings(settings);
		});
		connect(&ChatManager::instance(), &ChatManager::voiceSettingsChanged, this, [this] {
			const bool on = ChatManager::instance().voiceSettings().enabled;
			if (readAloud_->isChecked() != on) {
				const QSignalBlocker block(readAloud_);
				readAloud_->setChecked(on);
			}
		});
		return page;
	}

	void updateScene()
	{
		const QString name = ui::editedSceneName();
		addSources_->setText(name.isEmpty() ? i18n::tr("Wizard.Sources.AddNoScene")
						    : i18n::tr("Wizard.Sources.Add").arg(name));
	}

	void addSources()
	{
		const QStringList lines = ui::addMultichatSources(chatSource_->isChecked(), featuredSource_->isChecked());
		result_->setText(lines.join('\n'));
		result_->show();
		updateScene();
	}

	QWidget *donePage()
	{
		auto *page = new QWidget(this);
		auto *layout = new QVBoxLayout(page);
		layout->addWidget(title(i18n::tr("Wizard.Done.Title"), page));
		layout->addWidget(wrapped(i18n::tr("Wizard.Done.Text"), page));
		for (const char *tip : {"Wizard.Done.TipDock", "Wizard.Done.TipSettings", "Wizard.Done.TipHotkeys",
					"Wizard.Done.TipReopen"}) {
			auto *label = wrapped(QString::fromUtf8("•  ") + i18n::tr(tip), page);
			label->setContentsMargins(8, 0, 0, 0);
			layout->addWidget(label);
		}
		layout->addStretch(1);
		return page;
	}

	QStackedWidget *pages_ = nullptr;
	QPushButton *skip_ = nullptr;
	QPushButton *back_ = nullptr;
	QPushButton *next_ = nullptr;
	ChannelInput *input_ = nullptr;
	QLabel *added_ = nullptr;
	QCheckBox *chatSource_ = nullptr;
	QCheckBox *featuredSource_ = nullptr;
	QCheckBox *readAloud_ = nullptr;
	QPushButton *addSources_ = nullptr;
	QLabel *result_ = nullptr;
};

} // namespace

bool first_run_wizard_wanted()
{
	const ChatManager &m = ChatManager::instance();
	return m.chats().isEmpty() && !m.wizardShown();
}

void open_first_run_wizard(QWidget *parent)
{
	static QPointer<FirstRunWizard> wizard;
	if (!wizard)
		wizard = new FirstRunWizard(parent ? parent->window() : nullptr);
	wizard->show();
	wizard->raise();
	wizard->activateWindow();
}
