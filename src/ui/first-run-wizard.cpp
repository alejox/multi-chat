#include "first-run-wizard.hpp"
#include "settings-dialog.hpp"
#include "ui-common.hpp"
#include "../chat-manager.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QCheckBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cstring>

namespace {

enum class AddResult { Added, AlreadyThere, NoScene, Failed };

// The scene being edited: the preview in studio mode, otherwise the program scene. Adds a reference.
obs_source_t *editedScene()
{
	if (obs_frontend_preview_program_mode_active())
		if (obs_source_t *preview = obs_frontend_get_current_preview_scene())
			return preview;
	return obs_frontend_get_current_scene();
}

bool sceneHasSourceOfType(obs_scene_t *scene, const char *id)
{
	struct Search {
		const char *id;
		bool found;
	} search{id, false};
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *s = static_cast<Search *>(param);
			const char *itemId = obs_source_get_id(obs_sceneitem_get_source(item));
			if (itemId && std::strcmp(itemId, s->id) == 0)
				s->found = true;
			return !s->found;
		},
		&search);
	return search.found;
}

// Adds a source of type `id` to the edited scene, unless one is already there. A source of that
// type with the default name in another scene is reused (same settings, no duplicate names).
AddResult addToCurrentScene(const char *id, QString &sceneName)
{
	obs_source_t *sceneSource = editedScene();
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (!scene) {
		obs_source_release(sceneSource);
		return AddResult::NoScene;
	}
	sceneName = QString::fromUtf8(obs_source_get_name(sceneSource));
	if (sceneHasSourceOfType(scene, id)) {
		obs_source_release(sceneSource);
		return AddResult::AlreadyThere;
	}

	const QString base = QString::fromUtf8(obs_source_get_display_name(id));
	obs_source_t *source = nullptr;
	QString name = base;
	for (int n = 2; n < 100; ++n) {
		obs_source_t *existing = obs_get_source_by_name(name.toUtf8().constData());
		if (!existing)
			break;
		const char *existingId = obs_source_get_id(existing);
		if (existingId && std::strcmp(existingId, id) == 0) {
			source = existing; // keeps the reference
			break;
		}
		obs_source_release(existing);
		name = base + QStringLiteral(" %1").arg(n);
	}
	if (!source)
		source = obs_source_create(id, name.toUtf8().constData(), nullptr, nullptr);
	AddResult result = AddResult::Failed;
	if (source && obs_scene_add(scene, source))
		result = AddResult::Added;
	obs_source_release(source);
	obs_source_release(sceneSource);
	return result;
}

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
		setWindowTitle(ui::T("Wizard.Title"));
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
		skip_ = new QPushButton(ui::T("Wizard.Skip"), this);
		back_ = new QPushButton(ui::T("Wizard.Back"), this);
		next_ = new QPushButton(ui::T("Wizard.Next"), this);
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
		QDialog::done(result);
	}

private:
	void go(int index)
	{
		pages_->setCurrentIndex(index);
		const bool last = index == pages_->count() - 1;
		back_->setVisible(index > 0 && !last);
		skip_->setVisible(!last);
		next_->setText(ui::T(last ? "Wizard.Finish" : "Wizard.Next"));
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
		layout->addWidget(title(ui::T("Wizard.Welcome.Title"), page));
		layout->addWidget(wrapped(ui::T("Wizard.Welcome.Text"), page));
		layout->addStretch(1);
		return page;
	}

	QWidget *chatPage()
	{
		auto *page = new QWidget(this);
		auto *layout = new QVBoxLayout(page);
		layout->addWidget(title(ui::T("Wizard.Chat.Title"), page));
		layout->addWidget(wrapped(ui::T("Wizard.Chat.Text"), page));
		input_ = new ChannelInput(page);
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
		auto *demo = new QCheckBox(ui::T("Wizard.Chat.Demo"), page);
		demo->setChecked(ChatManager::instance().testMessages());
		connect(demo, &QCheckBox::toggled, this, [](bool on) { ChatManager::instance().setTestMessages(on); });
		layout->addWidget(demo);
		layout->addWidget(wrapped(ui::T("Wizard.Chat.Later"), page));
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
		added_->setText(names.isEmpty() ? QString() : ui::T("Wizard.Chat.Added").arg(names.join(", ")));
		added_->setVisible(!names.isEmpty());
	}

	QWidget *sourcesPage()
	{
		auto *page = new QWidget(this);
		auto *layout = new QVBoxLayout(page);
		layout->addWidget(title(ui::T("Wizard.Sources.Title"), page));
		layout->addWidget(wrapped(ui::T("Wizard.Sources.Text"), page));
		chatSource_ = new QCheckBox(QString::fromUtf8(obs_source_get_display_name("multichat_overlay")), page);
		chatSource_->setChecked(true);
		featuredSource_ =
			new QCheckBox(QString::fromUtf8(obs_source_get_display_name("multichat_featured")), page);
		layout->addWidget(chatSource_);
		layout->addWidget(featuredSource_);
		layout->addWidget(wrapped(ui::T("Wizard.Sources.FeaturedHelp"), page));
		addSources_ = new QPushButton(page);
		addSources_->setAutoDefault(false);
		auto *row = new QHBoxLayout();
		row->addWidget(addSources_);
		row->addStretch(1);
		layout->addLayout(row);
		result_ = wrapped(QString(), page);
		result_->hide();
		layout->addWidget(result_);
		layout->addStretch(1);
		connect(addSources_, &QPushButton::clicked, this, [this] { addSources(); });
		auto enable = [this] { addSources_->setEnabled(chatSource_->isChecked() || featuredSource_->isChecked()); };
		connect(chatSource_, &QCheckBox::toggled, this, enable);
		connect(featuredSource_, &QCheckBox::toggled, this, enable);
		return page;
	}

	void updateScene()
	{
		obs_source_t *scene = editedScene();
		const QString name = scene ? QString::fromUtf8(obs_source_get_name(scene)) : QString();
		obs_source_release(scene);
		addSources_->setText(name.isEmpty() ? ui::T("Wizard.Sources.AddNoScene")
						    : ui::T("Wizard.Sources.Add").arg(name));
	}

	void addSources()
	{
		QStringList lines;
		auto add = [&lines](const char *id) {
			QString scene;
			const QString name = QString::fromUtf8(obs_source_get_display_name(id));
			switch (addToCurrentScene(id, scene)) {
			case AddResult::Added:
				lines << ui::T("Wizard.Sources.Added").arg(name, scene);
				break;
			case AddResult::AlreadyThere:
				lines << ui::T("Wizard.Sources.AlreadyThere").arg(name, scene);
				break;
			case AddResult::NoScene:
				lines << ui::T("Wizard.Sources.NoScene");
				break;
			case AddResult::Failed:
				lines << ui::T("Wizard.Sources.Failed").arg(name);
				break;
			}
		};
		if (chatSource_->isChecked())
			add("multichat_overlay");
		if (featuredSource_->isChecked())
			add("multichat_featured");
		lines.removeDuplicates();
		result_->setText(lines.join('\n'));
		result_->show();
		updateScene();
	}

	QWidget *donePage()
	{
		auto *page = new QWidget(this);
		auto *layout = new QVBoxLayout(page);
		layout->addWidget(title(ui::T("Wizard.Done.Title"), page));
		layout->addWidget(wrapped(ui::T("Wizard.Done.Text"), page));
		for (const char *tip : {"Wizard.Done.TipDock", "Wizard.Done.TipSettings", "Wizard.Done.TipHotkeys",
					"Wizard.Done.TipReopen"}) {
			auto *label = wrapped(QString::fromUtf8("•  ") + ui::T(tip), page);
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
