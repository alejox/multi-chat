// Ventana "Multichat": añadir y quitar chats, y elegir cuándo conectarlos.

#include "chat-dock.hpp"
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
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>
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

		// Lista de chats.
		auto *chatsBox = new QGroupBox(ui::T("Settings.Chats"), this);
		auto *chatsLayout = new QVBoxLayout(chatsBox);
		list_ = new QListWidget(chatsBox);
		list_->setIconSize(QSize(18, 18));
		list_->setMinimumHeight(140);
		chatsLayout->addWidget(list_);

		auto *addRow = new QHBoxLayout();
		platform_ = new QComboBox(chatsBox);
		for (const char *p : {"twitch", "youtube", "kick", "tiktok"})
			platform_->addItem(QIcon(ui::platformIcon(p, 16, devicePixelRatioF())), chat::platformName(p),
					   QString::fromLatin1(p));
		channel_ = new QLineEdit(chatsBox);
		auto *add = new QPushButton(ui::T("Settings.Add"), chatsBox);
		addRow->addWidget(platform_);
		addRow->addWidget(channel_, 1);
		addRow->addWidget(add);
		chatsLayout->addLayout(addRow);

		auto *hintRow = new QHBoxLayout();
		hint_ = new QLabel(chatsBox);
		hint_->setWordWrap(true);
		hint_->setStyleSheet("color: palette(mid);");
		hintRow->addWidget(hint_, 1);
		chatsLayout->addLayout(hintRow);
		error_ = new QLabel(chatsBox);
		error_->setWordWrap(true);
		error_->setStyleSheet("color: #ff6b6b;");
		error_->hide();
		chatsLayout->addWidget(error_);
		root->addWidget(chatsBox);

		// Cuándo conectar.
		auto *modeBox = new QGroupBox(ui::T("Settings.When"), this);
		auto *modeLayout = new QVBoxLayout(modeBox);
		auto_ = new QRadioButton(ui::T("Settings.Mode.Auto"), modeBox);
		always_ = new QRadioButton(ui::T("Settings.Mode.Always"), modeBox);
		auto *autoHelp = new QLabel(ui::T("Settings.Mode.AutoHelp"), modeBox);
		autoHelp->setWordWrap(true);
		autoHelp->setStyleSheet("color: palette(mid); margin-left: 22px;");
		modeLayout->addWidget(auto_);
		modeLayout->addWidget(autoHelp);
		modeLayout->addWidget(always_);
		demo_ = new QCheckBox(ui::T("Settings.Demo"), modeBox);
		modeLayout->addWidget(demo_);
		root->addWidget(modeBox);

		auto *overlayHelp = new QLabel(ui::T("Settings.OverlayHelp"), this);
		overlayHelp->setWordWrap(true);
		root->addWidget(overlayHelp);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
		root->addWidget(buttons);

		ChatManager &m = ChatManager::instance();
		entries_ = m.entries();
		(m.mode() == ChatManager::Mode::Always ? always_ : auto_)->setChecked(true);
		demo_->setChecked(m.demo());

		connect(platform_, &QComboBox::currentIndexChanged, this, [this] { updateHint(); });
		connect(add, &QPushButton::clicked, this, [this] { addEntry(); });
		connect(channel_, &QLineEdit::returnPressed, this, [this] { addEntry(); });
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

private:
	void updateHint()
	{
		const QString p = platform_->currentData().toString();
		channel_->setPlaceholderText(ui::T(("Settings.Placeholder." + p).toUtf8().constData()));
		hint_->setText(ui::T(("Settings.Hint." + p).toUtf8().constData()));
	}

	void addEntry()
	{
		QString text = channel_->text().trimmed();
		if (text.isEmpty())
			return;
		// Si pegan una URL o "plataforma:canal" se respeta; si no, se usa la plataforma elegida.
		const bool explicitEntry = text.contains("://") || text.contains(".tv/") || text.contains(".com/") ||
					   text.contains("youtu.be/") || text.contains(':');
		const QString entry = explicitEntry ? text : platform_->currentData().toString() + ':' + text;
		chat::SourceInfo info;
		QString error;
		if (!chat::parseEntry(entry, info, error)) {
			error_->setText(error);
			error_->show();
			return;
		}
		error_->hide();
		channel_->clear();
		entries_ << entry;
		apply();
	}

	void removeEntry(const QString &entry)
	{
		if (entry.isEmpty())
			return;
		entries_.removeAll(entry);
		apply();
	}

	void apply()
	{
		ChatManager &m = ChatManager::instance();
		const QStringList errors = m.configure(
			entries_, always_->isChecked() ? ChatManager::Mode::Always : ChatManager::Mode::Auto,
			demo_->isChecked());
		m.save();
		entries_ = m.entries();
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
		for (const QString &entry : entries_) {
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

	QStringList entries_;
	QListWidget *list_ = nullptr;
	QComboBox *platform_ = nullptr;
	QLineEdit *channel_ = nullptr;
	QLabel *hint_ = nullptr;
	QLabel *error_ = nullptr;
	QRadioButton *auto_ = nullptr;
	QRadioButton *always_ = nullptr;
	QCheckBox *demo_ = nullptr;
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
