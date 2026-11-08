#include "chat-dock.hpp"
#include "dock-prefs.hpp"
#include "first-run-wizard.hpp"
#include "flow-layout.hpp"
#include "image-cache.hpp"
#include "ui-common.hpp"
#include "../i18n.hpp"
#include "../chat-manager.hpp"
#include "../config-store.hpp"
#include "../chat/message.hpp"
#include "../updater.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDockWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QTextBlock>
#include <QTextBlockUserData>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

// ---------------------------------------------------------------------------------------
// ChatDock

namespace {
QPointer<ChatDock> theDock; // the one OBS shows (plugin-main creates it once)
} // namespace

ChatDock::ChatDock(QWidget *parent) : QWidget(parent)
{
	theDock = this;
	const QJsonObject prefs = ui::loadDockPrefs();
	fontSize_ = prefs.value("fontSize").toInt(11);
	timestamps_ = prefs.value("timestamps").toBool(false);
	for (const QJsonValue v : prefs.value("hidden").toArray())
		hidden_.insert(v.toString());

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);

	auto *header = new QHBoxLayout();
	header->setSpacing(2);
	status_ = new QLabel(this);
	status_->setTextFormat(Qt::PlainText);
	statusEye_ = new QLabel(this);
	statusViewers_ = new QLabel(this);
	statusViewers_->setTextFormat(Qt::PlainText);
	auto *statusRow = new QHBoxLayout;
	statusRow->setSpacing(4);
	statusRow->addWidget(status_);
	statusRow->addSpacing(4);
	statusRow->addWidget(statusEye_);
	statusRow->addWidget(statusViewers_);
	statusRow->addStretch(1);
	header->addLayout(statusRow, 1);
	status_->setAccessibleName(i18n::tr("Dock.Status"));
	// Monochrome SVG icons tinted with the theme (applyTheme); the tooltip names the action.
	auto button = [&](const QString &icon, const char *tip, auto slot) {
		auto *b = new QToolButton(this);
		b->setToolTip(i18n::tr(tip));
		b->setAccessibleName(i18n::tr(tip));
		b->setAutoRaise(true);
		b->setIconSize(QSize(16, 16));
		connect(b, &QToolButton::clicked, this, slot);
		header->addWidget(b);
		iconButtons_.append({b, icon});
		return b;
	};
	button("search", "Dock.Search", [this] { openSearch(); });
	button("text-smaller", "Dock.Smaller", [this] { changeFont(-1); });
	button("text-bigger", "Dock.Bigger", [this] { changeFont(1); });
	button("clear", "Dock.Clear", [this] { clearWithUndo(); });
	// The gear opens a menu (rebuilt every time, so the ticks are current): settings and the
	// actions that do not deserve a button of their own.
	QToolButton *gear = button("settings", "Dock.Settings", [] {});
	auto *gearMenu = new QMenu(gear);
	connect(gearMenu, &QMenu::aboutToShow, this, [this, gearMenu] {
		gearMenu->clear();
		fillGearMenu(gearMenu);
	});
	gear->setMenu(gearMenu);
	gear->setPopupMode(QToolButton::InstantPopup);
	root->addLayout(header);

	banner_ = new QWidget(this);
	banner_->setObjectName("multichatBanner");
	banner_->setAttribute(Qt::WA_StyledBackground);
	auto *bannerLayout = new QHBoxLayout(banner_);
	bannerLayout->setContentsMargins(6, 3, 6, 3);
	bannerLayout->setSpacing(4);
	bannerText_ = new QLabel(i18n::tr("Dock.Multistream"), banner_);
	QFont bold = bannerText_->font();
	bold.setBold(true);
	bannerText_->setFont(bold);
	bannerLayout->addWidget(bannerText_);
	for (int i = 0; i < 4; ++i) {
		auto *icon = new QLabel(banner_);
		bannerLayout->addWidget(icon);
		bannerIcons_ << icon;
	}
	bannerLayout->addStretch(1);
	banner_->hide();
	root->addWidget(banner_);

	// Aviso de versión nueva en las Releases de GitHub.
	auto *update = new QWidget(this);
	update_ = update;
	update->setObjectName("multichatUpdate");
	update->setAttribute(Qt::WA_StyledBackground);
	auto *updateLayout = new QHBoxLayout(update);
	updateLayout->setContentsMargins(6, 3, 3, 3);
	auto *updateText = new QLabel(update);
	updateText->setWordWrap(true);
	auto *download = new QPushButton(i18n::tr("Update.Now"), update);
	updateLayout->addWidget(updateText, 1);
	updateLayout->addWidget(download);
	update->hide();
	root->addWidget(update);
	auto showUpdate = [update, updateText, download](const chat::Release &release) {
		if (release.version.isEmpty())
			return;
		updateText->setText(i18n::tr("Update.Available").arg(release.version));
		update->show();
	};
	connect(download, &QPushButton::clicked, this, [] { Updater::instance().update(); });
	connect(&Updater::instance(), &Updater::updateAvailable, this, showUpdate);
	showUpdate(Updater::instance().available());

	chips_ = new QWidget(this);
	chipsLayout_ = new FlowLayout(chips_, 4);
	root->addWidget(chips_);

	errors_ = new QLabel(this);
	errors_->setWordWrap(true);
	errors_->hide();
	root->addWidget(errors_);

	// "Chat cleared · Undo" for a few seconds instead of asking before clearing.
	undoBar_ = new QWidget(this);
	undoBar_->setObjectName("multichatUndo");
	undoBar_->setAttribute(Qt::WA_StyledBackground);
	undoBar_->setStyleSheet("#multichatUndo { background-color: palette(alternate-base); border-radius: 4px; }");
	auto *undoLayout = new QHBoxLayout(undoBar_);
	undoLayout->setContentsMargins(6, 3, 3, 3);
	undoLayout->addWidget(new QLabel(i18n::tr("Dock.Cleared"), undoBar_), 1);
	auto *undoButton = new QPushButton(i18n::tr("Dock.Undo"), undoBar_);
	undoLayout->addWidget(undoButton);
	undoBar_->hide();
	root->addWidget(undoBar_);
	undoTimer_.setSingleShot(true);
	undoTimer_.setInterval(6000);
	connect(&undoTimer_, &QTimer::timeout, this, [this] {
		undoBar_->hide();
		ChatManager::instance().discardUndo();
	});
	connect(undoButton, &QPushButton::clicked, this, [this] {
		undoTimer_.stop();
		undoBar_->hide();
		ChatManager::instance().undoClear();
	});

	// Ctrl+F: filter by text or name; Esc closes it.
	searchBar_ = new QWidget(this);
	auto *searchLayout = new QHBoxLayout(searchBar_);
	searchLayout->setContentsMargins(0, 0, 0, 0);
	searchLayout->setSpacing(4);
	searchEdit_ = new QLineEdit(searchBar_);
	searchEdit_->setPlaceholderText(i18n::tr("Dock.Search.Placeholder"));
	searchEdit_->setAccessibleName(i18n::tr("Dock.Search"));
	searchEdit_->setClearButtonEnabled(true);
	searchCount_ = new QLabel(searchBar_);
	auto *searchClose = new QToolButton(searchBar_);
	searchClose->setAutoRaise(true);
	searchClose->setIconSize(QSize(14, 14));
	iconButtons_.append({searchClose, "close"});
	searchClose->setToolTip(i18n::tr("Dock.Search.Close"));
	searchClose->setAccessibleName(i18n::tr("Dock.Search.Close"));
	searchLayout->addWidget(searchEdit_, 1);
	searchLayout->addWidget(searchCount_);
	searchLayout->addWidget(searchClose);
	searchBar_->hide();
	root->addWidget(searchBar_);
	connect(searchClose, &QToolButton::clicked, this, &ChatDock::closeSearch);
	auto *searchEsc = new QShortcut(QKeySequence(Qt::Key_Escape), searchEdit_, nullptr, nullptr, Qt::WidgetShortcut);
	connect(searchEsc, &QShortcut::activated, this, &ChatDock::closeSearch);
	auto *find = new QShortcut(QKeySequence::Find, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
	connect(find, &QShortcut::activated, this, &ChatDock::openSearch);

	empty_ = new QWidget(this);
	auto *emptyLayout = new QVBoxLayout(empty_);
	auto *emptyText = new QLabel(i18n::tr("Dock.Empty"), empty_);
	emptyText->setWordWrap(true);
	emptyText->setAlignment(Qt::AlignCenter);
	auto *emptyButton = new QPushButton(i18n::tr("Dock.Configure"), empty_);
	connect(emptyButton, &QPushButton::clicked, this, [this] { open_multichat_settings(this); });
	emptyLayout->addStretch(1);
	emptyLayout->addWidget(emptyText);
	emptyLayout->addWidget(emptyButton, 0, Qt::AlignHCenter);
	emptyLayout->addStretch(1);
	root->addWidget(empty_, 1);

	view_ = new ChatView(this);
	root->addWidget(view_, 1);
	view_->setFontSize(fontSize_);
	view_->setHiddenSources(hidden_);
	view_->setShowTimestamps(timestamps_);
	connect(searchEdit_, &QLineEdit::textChanged, view_, &ChatView::setSearch);
	connect(view_, &ChatView::contentChanged, this, &ChatDock::updateSearchCount);

	applyTheme();

	ChatManager &manager = ChatManager::instance();
	QList<QJsonObject> history;
	for (const QJsonValue v : manager.history())
		history << v.toObject();
	view_->setMessages(history);
	connect(&manager, &ChatManager::message, view_, &ChatView::addMessage);
	connect(&manager, &ChatManager::cleared, view_, [this] { view_->setMessages({}); });
	connect(&manager, &ChatManager::restored, view_, [this] {
		QList<QJsonObject> restored;
		for (const QJsonValue v : ChatManager::instance().history())
			restored << v.toObject();
		view_->setMessages(restored);
	});
	connect(view_, &ChatView::menuRequested, this, &ChatDock::showMenu);
	connect(&manager, &ChatManager::deleted, view_, &ChatView::removeMessages);
	connect(&manager, &ChatManager::stateChanged, this, &ChatDock::updateState);
	updateState(manager.state());

	// Clic en un mensaje: lo destaca (reemplaza al anterior); clic en el destacado: lo quita.
	view_->setFeatured(manager.featured().value("id").toString());
	connect(&manager, &ChatManager::featuredChanged, view_,
		[this](const QJsonObject &m) { view_->setFeatured(m.value("id").toString()); });
	connect(view_, &ChatView::messageClicked, this, [](const QJsonObject &m) {
		ChatManager &mgr = ChatManager::instance();
		const QString id = m.value("id").toString();
		if (!id.isEmpty() && mgr.featured().value("id").toString() == id)
			mgr.clearFeatured();
		else
			mgr.feature(m);
	});
}

void ChatDock::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape && searchBar_->isVisible()) {
		closeSearch();
		event->accept();
		return;
	}
	if (event->key() == Qt::Key_Escape && !ChatManager::instance().featured().isEmpty()) {
		ChatManager::instance().clearFeatured();
		event->accept();
		return;
	}
	QWidget::keyPressEvent(event);
}

void ChatDock::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
		applyTheme();
}

void ChatDock::applyTheme()
{
	if (!view_)
		return; // still being built
	const QPalette pal = palette();
	const qreal dpr = devicePixelRatioF();
	const QColor text = pal.color(QPalette::ButtonText);
	for (const auto &[button, icon] : iconButtons_) {
		const QIcon themed = ui::themedIcon(icon, text, button->iconSize().width(), dpr);
		if (themed.isNull())
			button->setText(button->toolTip().left(1)); // icon files missing: still clickable
		else
			button->setIcon(themed);
	}
	const QColor window = pal.color(QPalette::Window);
	auto tint = [&window](const QColor &accent, float amount) {
		return ui::blend(window, accent, amount).name();
	};
	banner_->setStyleSheet(QStringLiteral("#multichatBanner { background-color: %1; border-radius: 4px; }")
				       .arg(tint(QColor(145, 70, 255), 0.3f)));
	update_->setStyleSheet(QStringLiteral("#multichatUpdate { background-color: %1; border-radius: 4px; }")
				       .arg(tint(QColor(62, 207, 110), 0.25f)));
	errors_->setStyleSheet(QStringLiteral("color: %1;").arg(ui::alertColor(window).name()));
	updateState(ChatManager::instance().state());
}

// OBS wraps the widget in its own QDockWidget (obs_frontend_add_dock_by_id); the frontend API
// has no call to show a dock, so the wrapper is found walking up from the widget.
void show_multichat_dock()
{
	if (!theDock)
		return;
	for (QWidget *w = theDock->parentWidget(); w; w = w->parentWidget()) {
		if (auto *dock = qobject_cast<QDockWidget *>(w)) {
			dock->setVisible(true);
			dock->raise();
			return;
		}
	}
	theDock->show();
}

void ChatDock::fillGearMenu(QMenu *menu)
{
	menu->addAction(i18n::tr("Dock.Menu.Configure"), this, [this] { open_multichat_settings(this); });
	menu->addSeparator();
	QAction *testMessages = menu->addAction(i18n::tr("Dock.Menu.TestMessages"));
	testMessages->setCheckable(true);
	testMessages->setChecked(ChatManager::instance().testMessages());
	connect(testMessages, &QAction::toggled, this, [](bool on) { ChatManager::instance().setTestMessages(on); });
	QAction *time = menu->addAction(i18n::tr("Dock.Menu.ShowTime"));
	time->setCheckable(true);
	time->setChecked(timestamps_);
	connect(time, &QAction::toggled, this, [this](bool on) { setShowTimestamps(on); });
	QMenu *size = menu->addMenu(i18n::tr("Dock.Menu.TextSize"));
	size->addAction(i18n::tr("Dock.Bigger"), this, [this] { changeFont(1); });
	size->addAction(i18n::tr("Dock.Smaller"), this, [this] { changeFont(-1); });
	menu->addAction(i18n::tr("Dock.Refresh"), this, [] { ChatManager::instance().refresh(); });
	menu->addSeparator();
	menu->addAction(i18n::tr("Dock.Menu.Wizard"), this, [this] { open_first_run_wizard(this); });
	menu->addAction(i18n::tr("Dock.Menu.CheckUpdates"), this, [] { Updater::instance().check(true); });
}

void ChatDock::clearWithUndo()
{
	ChatManager::instance().clear();
	undoBar_->show();
	undoTimer_.start();
}

void ChatDock::showMenu(const QJsonObject &message, const QPoint &globalPos)
{
	QMenu menu(this);
	ChatManager &manager = ChatManager::instance();
	if (!message.isEmpty()) {
		const QString id = message.value("id").toString();
		const bool featured = !id.isEmpty() && manager.featured().value("id").toString() == id;
		menu.addAction(i18n::tr(featured ? "Dock.Menu.Unfeature" : "Dock.Menu.Feature"), this,
			       [message, featured] {
				       if (featured)
					       ChatManager::instance().clearFeatured();
				       else
					       ChatManager::instance().feature(message);
			       });
		QAction *hide = menu.addAction(i18n::tr("Dock.Menu.HideMessage"), this,
					       [message] { ChatManager::instance().hideMessage(message); });
		hide->setEnabled(!chat::hideMessage(message).isEmpty());
		QString author = message.value("author").toString();
		author.replace('&', "&&"); // not a mnemonic
		QAction *hideUser = menu.addAction(i18n::tr("Dock.Menu.HideUser").arg(author), this,
						   [message] { ChatManager::instance().hideUser(message); });
		hideUser->setEnabled(!author.isEmpty());
		menu.addSeparator();
		menu.addAction(i18n::tr("Dock.Menu.CopyMessage"), this,
			       [message] { QGuiApplication::clipboard()->setText(chat::plainText(message)); });
		menu.addAction(i18n::tr("Dock.Menu.CopyName"), this, [message] {
			QGuiApplication::clipboard()->setText(message.value("author").toString());
		});
	}
	if (view_->textCursor().hasSelection())
		menu.addAction(i18n::tr("Dock.Menu.CopySelection"), view_, &ChatView::copy);
	if (!menu.isEmpty())
		menu.addSeparator();
	QAction *search = menu.addAction(i18n::tr("Dock.Search"), this, &ChatDock::openSearch);
	search->setShortcut(QKeySequence::Find);
	QAction *time = menu.addAction(i18n::tr("Dock.Menu.ShowTime"));
	time->setCheckable(true);
	time->setChecked(timestamps_);
	connect(time, &QAction::toggled, this, [this](bool on) { setShowTimestamps(on); });
	menu.exec(globalPos);
}

void ChatDock::setShowTimestamps(bool show)
{
	timestamps_ = show;
	view_->setShowTimestamps(show);
	QJsonObject prefs = ui::loadDockPrefs();
	prefs.insert("timestamps", show);
	ui::saveDockPrefs(prefs);
}

void ChatDock::openSearch()
{
	searchBar_->show();
	searchEdit_->setFocus(Qt::ShortcutFocusReason);
	searchEdit_->selectAll();
	updateSearchCount();
}

void ChatDock::closeSearch()
{
	searchEdit_->clear(); // shows every message again
	searchBar_->hide();
	view_->setFocus(Qt::OtherFocusReason);
}

void ChatDock::updateSearchCount()
{
	if (!searchBar_->isVisible() || searchEdit_->text().trimmed().isEmpty()) {
		searchCount_->clear();
		return;
	}
	const int n = view_->shownCount();
	searchCount_->setText(n == 0 ? i18n::tr("Dock.Search.None") : i18n::tr("Dock.Search.Count").arg(n));
}

void ChatDock::changeFont(int delta)
{
	fontSize_ = std::clamp(fontSize_ + delta, 8, 24);
	view_->setFontSize(fontSize_);
	QJsonObject prefs = ui::loadDockPrefs();
	prefs.insert("fontSize", fontSize_);
	ui::saveDockPrefs(prefs);
}

namespace {

// A tinted SVG from data/icons as a pixmap; null if the file is missing.
QPixmap tintedPixmap(const QString &name, const QColor &color, int size, qreal dpr)
{
	return ui::themedIcon(name, color, size, dpr).pixmap(QSize(size, size), dpr);
}

// Shows `pixmap`, or `fallback` as text when the icon could not be loaded.
void setIconLabel(QLabel *label, const QPixmap &pixmap, const QString &fallback)
{
	if (pixmap.isNull())
		label->setText(fallback);
	else
		label->setPixmap(pixmap);
}

// A chat chip is a checkable QToolButton whose contents (status icon, channel, live dot, eye and
// viewers) are labels in its own layout, so the dot and the eye are tinted icons, not emoji.
QLabel *chipPart(QToolButton *chip, const char *name)
{
	return chip->findChild<QLabel *>(QLatin1String(name), Qt::FindDirectChildrenOnly);
}

void buildChip(QToolButton *chip)
{
	auto *row = new QHBoxLayout(chip);
	row->setContentsMargins(6, 3, 6, 3);
	row->setSpacing(4);
	for (const char *name : {"icon", "name", "live", "eye", "viewers"}) {
		auto *label = new QLabel(chip);
		label->setObjectName(QLatin1String(name));
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		label->setForegroundRole(QPalette::ButtonText);
		row->addWidget(label);
	}
	chipPart(chip, "name")->setTextFormat(Qt::PlainText);
	chipPart(chip, "viewers")->setTextFormat(Qt::PlainText);
}

} // namespace

void ChatDock::updateState(const QJsonObject &state)
{
	const QJsonArray sources = state.value("sources").toArray();
	const QJsonArray live = state.value("livePlatforms").toArray();
	const qreal dpr = devicePixelRatioF();

	const QColor textColor = palette().color(QPalette::ButtonText);
	const QColor alert = ui::alertColor(palette().color(QPalette::Window));
	const QPixmap eye = tintedPixmap(QStringLiteral("eye"), textColor, 14, dpr);
	const QPixmap liveDot = tintedPixmap(QStringLiteral("live"), alert, 10, dpr);

	// Estado general, con el total de espectadores de todas las plataformas.
	const int viewers = state.value("viewers").toInt(-1);
	const bool streaming = state.value("streaming").toBool();
	bool showViewers = viewers >= 0;
	if (streaming) {
		const QString service = state.value("service").toString();
		status_->setText(QString::fromUtf8("● ") + i18n::tr("Dock.OnAir") +
				 (service.isEmpty() ? QString() : QStringLiteral(" · ") + service));
		const QString style = QStringLiteral("color: %1; font-weight: bold;").arg(alert.name());
		status_->setStyleSheet(style);
		statusViewers_->setStyleSheet(style);
		setIconLabel(statusEye_, tintedPixmap(QStringLiteral("eye"), alert, 14, dpr), QString::fromUtf8("👁"));
	} else {
		QStringList names;
		for (const QJsonValue p : live)
			names << chat::platformName(p.toString());
		status_->setText(names.isEmpty() ? i18n::tr("Dock.OffAir") : i18n::tr("Dock.LiveOn").arg(names.join(", ")));
		status_->setStyleSheet(QString());
		statusViewers_->setStyleSheet(QString());
		setIconLabel(statusEye_, eye, QString::fromUtf8("👁"));
		showViewers = showViewers && !names.isEmpty();
	}
	statusViewers_->setText(showViewers ? ui::formatCount(viewers) : QString());
	statusEye_->setVisible(showViewers);
	statusViewers_->setVisible(showViewers);
	statusEye_->setToolTip(showViewers ? i18n::tr("Dock.Viewers").arg(ui::formatCount(viewers)) : QString());

	// Aviso de multistream con los logos de las plataformas en vivo.
	const bool multi = state.value("multistream").toBool();
	banner_->setVisible(multi);
	for (int i = 0; i < bannerIcons_.size(); ++i) {
		const bool has = i < live.size();
		bannerIcons_[i]->setVisible(has);
		if (has)
			bannerIcons_[i]->setPixmap(ui::platformIcon(live[i].toString(), 16, dpr));
	}

	// One button per chat: click to hide or show its messages. Updated in place by key, so
	// focus and the hover state survive the frequent state updates.
	QStringList order;
	for (const QJsonValue v : sources) {
		const QJsonObject s = v.toObject();
		const QString key = s.value("key").toString();
		const QString platform = s.value("platform").toString();
		const QString status = s.value("status").toString();
		order << key;
		QToolButton *chip = chipByKey_.value(key);
		if (!chip) {
			chip = new QToolButton(chips_);
			chip->setCheckable(true);
			buildChip(chip);
			connect(chip, &QToolButton::toggled, this, [this, key](bool shown) {
				if (shown)
					hidden_.remove(key);
				else
					hidden_.insert(key);
				view_->setHiddenSources(hidden_);
				QJsonObject prefs = ui::loadDockPrefs();
				QJsonArray list;
				for (const QString &h : hidden_)
					list.append(h);
				prefs.insert("hidden", list);
				ui::saveDockPrefs(prefs);
			});
			chipByKey_.insert(key, chip);
		}
		const int chipViewers = s.value("viewers").toInt(-1);
		const bool chipLive = s.value("live").toInt() == 1;
		chipPart(chip, "icon")->setPixmap(ui::statusIcon(platform, status, 16, dpr).pixmap(QSize(16, 16), dpr));
		chipPart(chip, "name")->setText(s.value("channel").toString());
		QLabel *liveLabel = chipPart(chip, "live");
		setIconLabel(liveLabel, liveDot, QString::fromUtf8("●"));
		liveLabel->setVisible(chipLive);
		QLabel *eyeLabel = chipPart(chip, "eye");
		setIconLabel(eyeLabel, eye, QString::fromUtf8("👁"));
		eyeLabel->setVisible(chipViewers >= 0);
		QLabel *viewersLabel = chipPart(chip, "viewers");
		viewersLabel->setText(chipViewers >= 0 ? ui::formatCount(chipViewers) : QString());
		viewersLabel->setVisible(chipViewers >= 0);
		chip->layout()->invalidate();
		chip->setMinimumSize(chip->layout()->sizeHint());
		{
			const QSignalBlocker block(chip);
			chip->setChecked(!hidden_.contains(key));
		}
		const QString statusText = i18n::tr(("Status." + status).toUtf8().constData());
		QString tip = chat::platformName(platform) + " · " + s.value("channel").toString() + "\n" + statusText;
		const int liveState = s.value("live").toInt();
		tip += " · " + i18n::tr(liveState == 1 ? "Live.Yes" : liveState == 0 ? "Live.No" : "Live.Unknown");
		if (chipViewers >= 0)
			tip += " · " + i18n::tr("Dock.Viewers").arg(ui::formatCount(chipViewers));
		if (!s.value("detail").toString().isEmpty())
			tip += "\n" + s.value("detail").toString();
		tip += "\n" + i18n::tr("Dock.ChipHint");
		chip->setToolTip(tip);
		chip->setAccessibleName(chat::platformName(platform) + " " + s.value("channel").toString());
		chip->setAccessibleDescription(statusText);
	}
	// Chats no longer configured go away.
	for (auto it = chipByKey_.begin(); it != chipByKey_.end();) {
		if (order.contains(it.key())) {
			++it;
			continue;
		}
		chipsLayout_->removeWidget(it.value());
		it.value()->deleteLater();
		it = chipByKey_.erase(it);
	}
	// Same order as the settings; only re-laid out when it changed.
	QStringList current;
	for (int i = 0; i < chipsLayout_->count(); ++i)
		current << chipByKey_.key(qobject_cast<QToolButton *>(chipsLayout_->itemAt(i)->widget()));
	if (current != order) {
		while (QLayoutItem *item = chipsLayout_->takeAt(0))
			delete item; // the widget stays
		for (const QString &key : order)
			chipsLayout_->addWidget(chipByKey_.value(key));
	}
	chips_->setVisible(!sources.isEmpty());

	QStringList errors;
	for (const QJsonValue e : state.value("errors").toArray())
		errors << e.toString();
	errors_->setText(errors.join("\n"));
	errors_->setVisible(!errors.isEmpty());

	empty_->setVisible(sources.isEmpty());
	view_->setVisible(!sources.isEmpty());
}
