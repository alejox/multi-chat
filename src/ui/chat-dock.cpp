#include "chat-dock.hpp"
#include "flow-layout.hpp"
#include "image-cache.hpp"
#include "ui-common.hpp"
#include "../chat-manager.hpp"
#include "../chat/message.hpp"
#include "../updater.hpp"

#include <obs-module.h>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
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

namespace {

constexpr int kMaxMessages = 300;

QString prefsPath()
{
	char *path = obs_module_config_path("dock.json");
	QString out = QString::fromUtf8(path);
	bfree(path);
	return out;
}

QJsonObject loadPrefs()
{
	QFile file(prefsPath());
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return QJsonDocument::fromJson(file.readAll()).object();
}

void savePrefs(const QJsonObject &prefs)
{
	const QString path = prefsPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		file.write(QJsonDocument(prefs).toJson());
}

// Lo que se guarda en cada bloque (un mensaje) del documento: su id y su fondo sin destacar.
class MessageData : public QTextBlockUserData {
public:
	QString id;
	QColor background; // inválido = sin fondo
};

// Aplica el fondo de un bloque: el de destacado o el suyo de siempre.
void setBlockBackground(const QTextBlock &block, const QColor &color)
{
	QTextBlockFormat format = block.blockFormat();
	if (color.isValid())
		format.setBackground(color);
	else
		format.clearBackground();
	QTextCursor(block).setBlockFormat(format);
}

} // namespace

// ---------------------------------------------------------------------------------------
// ChatView

ChatView::ChatView(QWidget *parent) : QTextBrowser(parent)
{
	// Los enlaces del chat se abren en el navegador; solo http y https.
	setOpenLinks(false);
	setOpenExternalLinks(false);
	connect(this, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
		if (url.scheme() == "http" || url.scheme() == "https")
			QDesktopServices::openUrl(url);
	});
	setFrameShape(QFrame::NoFrame);
	setUndoRedoEnabled(false);
	document()->setMaximumBlockCount(kMaxMessages);
	document()->setDocumentMargin(4);
	viewport()->setToolTip(ui::T("Dock.FeatureHint"));

	connect(&ImageCache::instance(), &ImageCache::ready, this, &ChatView::imageReady);

	// Scrolled up while messages keep arriving: a pill counts them and jumps back down.
	pill_ = new QToolButton(this);
	pill_->setCursor(Qt::PointingHandCursor);
	pill_->setStyleSheet("QToolButton { background-color: palette(highlight); color: palette(highlighted-text); "
			     "border: none; border-radius: 10px; padding: 2px 10px; font-weight: bold; }");
	pill_->hide();
	connect(pill_, &QToolButton::clicked, this, &ChatView::scrollToBottom);
	connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
		if (unread_ > 0 && isAtBottom()) {
			unread_ = 0;
			updatePill();
		}
	});
}

bool ChatView::isAtBottom() const
{
	const QScrollBar *bar = verticalScrollBar();
	return bar->value() >= bar->maximum() - 4;
}

void ChatView::scrollToBottom()
{
	verticalScrollBar()->setValue(verticalScrollBar()->maximum());
	unread_ = 0;
	updatePill();
}

void ChatView::updatePill()
{
	if (unread_ <= 0) {
		pill_->hide();
		return;
	}
	const QString text = ui::T("Dock.NewMessages").arg(unread_);
	pill_->setText(text);
	pill_->setAccessibleName(text);
	pill_->adjustSize();
	const QRect area = viewport()->geometry();
	pill_->move(area.center().x() - pill_->width() / 2, area.bottom() - pill_->height() - 6);
	pill_->show();
	pill_->raise();
}

void ChatView::resizeEvent(QResizeEvent *event)
{
	QTextBrowser::resizeEvent(event);
	if (pill_->isVisible())
		updatePill();
}

// An emote or badge image arrived: relayout only the messages that use it, keeping the view
// still when they are above it.
void ChatView::imageReady(const QString &url)
{
	QScrollBar *bar = verticalScrollBar();
	const bool atBottom = isAtBottom();
	int keep = bar->value();
	document()->addResource(QTextDocument::ImageResource, QUrl(url), ImageCache::instance().get(url));
	QAbstractTextDocumentLayout *layout = document()->documentLayout();
	for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
		bool uses = false;
		for (auto it = block.begin(); !it.atEnd() && !uses; ++it) {
			const QTextCharFormat format = it.fragment().charFormat();
			uses = format.isImageFormat() && format.toImageFormat().name() == url;
		}
		if (!uses)
			continue;
		const QRectF before = layout->blockBoundingRect(block);
		document()->markContentsDirty(block.position(), block.length());
		const QRectF after = layout->blockBoundingRect(block);
		if (before.bottom() <= keep)
			keep += qRound(after.height() - before.height());
	}
	bar->setValue(atBottom ? bar->maximum() : std::max(0, keep));
}

QVariant ChatView::loadResource(int type, const QUrl &name)
{
	if (type == QTextDocument::ImageResource) {
		if (name.scheme() == "mc-icon")
			return ui::platformIcon(name.path(), emoteSize_, devicePixelRatioF());
		if (name.scheme() == "https") {
			QImage image = ImageCache::instance().get(name.toString());
			if (!image.isNull())
				return image;
			QImage placeholder(1, 1, QImage::Format_ARGB32);
			placeholder.fill(Qt::transparent);
			return placeholder; // se reemplaza cuando llega la imagen
		}
	}
	return QTextBrowser::loadResource(type, name);
}

void ChatView::changeEvent(QEvent *event)
{
	QTextBrowser::changeEvent(event);
	// Cambió el tema de OBS: los colores dependen del fondo.
	if (event->type() == QEvent::PaletteChange)
		rebuild();
}

QString ChatView::html(const QJsonObject &m) const
{
	const QColor base = palette().color(QPalette::Base);
	const QColor muted = ui::blend(palette().color(QPalette::Text), base, 0.45f);
	const QString platform = m.value("platform").toString();
	const int icon = qRound(emoteSize_ * 0.75);
	QString out;

	const QString system = m.value("system").toString();
	if (!system.isEmpty())
		out += QStringLiteral("<span style=\"color:%1; font-style:italic;\">%2</span><br>")
			       .arg(muted.name(), system.toHtmlEscaped());

	if (timestamps_) {
		const double ts = m.value("ts").toDouble();
		if (ts > 0) {
			const QTime time = QDateTime::fromMSecsSinceEpoch(qint64(ts * 1000)).time();
			out += QStringLiteral("<span style=\"color:%1; font-size:small;\">%2</span>&nbsp;")
				       .arg(muted.name(), QLocale().toString(time, QLocale::ShortFormat).toHtmlEscaped());
		}
	}
	out += QStringLiteral("<img src=\"mc-icon:%1\" width=\"%2\" height=\"%2\">&nbsp;").arg(platform).arg(icon);

	for (const QJsonValue b : m.value("badges").toArray()) {
		QString label;
		QColor bg;
		if (ui::badgeStyle(b.toString(), label, bg))
			out += QStringLiteral("<span style=\"background-color:%1; color:#ffffff; font-size:small; "
					      "font-weight:bold;\">&nbsp;%2&nbsp;</span>&nbsp;")
				       .arg(bg.name(), label.toHtmlEscaped());
	}
	const QString amount = m.value("amount").toString();
	if (!amount.isEmpty())
		out += QStringLiteral("<span style=\"background-color:%1; color:#000000; font-weight:bold;\">"
				      "&nbsp;%2&nbsp;</span>&nbsp;")
			       .arg(ui::platformColor(platform).name(), amount.toHtmlEscaped());

	const QString author = m.value("author").toString();
	const QColor color = ui::readableColor(m.value("color").toString(), author, base);
	out += QStringLiteral("<span style=\"color:%1; font-weight:bold;\">%2</span>")
		       .arg(color.name(), author.toHtmlEscaped());

	const QJsonArray parts = m.value("parts").toArray();
	if (!parts.isEmpty()) {
		const bool action = m.value("action").toBool();
		out += action ? QStringLiteral(" <span style=\"color:%1;\">").arg(color.name())
			      : QStringLiteral("<span style=\"color:%1;\">:</span> <span>").arg(muted.name());
		for (const QJsonValue v : parts) {
			const QJsonObject p = v.toObject();
			if (p.value("type").toString() == "emote")
				out += QStringLiteral("<img src=\"%1\" height=\"%2\" title=\"%3\">")
					       .arg(p.value("url").toString().toHtmlEscaped())
					       .arg(emoteSize_)
					       .arg(p.value("name").toString().toHtmlEscaped());
			else
				out += chat::linkifyHtml(p.value("text").toString(),
							 palette().color(QPalette::Link).name());
		}
		out += QStringLiteral("</span>");
	}
	return out;
}

void ChatView::addMessage(const QJsonObject &m)
{
	messages_.append(m);
	while (messages_.size() > kMaxMessages)
		messages_.removeFirst();
	if (hidden_.contains(m.value("source").toString()) || !chat::matchesSearch(m, search_))
		return;

	QScrollBar *bar = verticalScrollBar();
	const bool atBottom = isAtBottom();
	int keep = bar->value();
	// At the block limit the oldest message goes away: compensate so the view stays put.
	if (!atBottom && document()->blockCount() >= kMaxMessages)
		keep -= qRound(document()->documentLayout()->blockBoundingRect(document()->firstBlock()).height());
	append(html(m)); // QTextEdit::append sigue abajo solo si ya estaba abajo

	auto *data = new MessageData;
	data->id = m.value("id").toString();
	QTextBlockFormat format;
	format.setTopMargin(2);
	format.setBottomMargin(2);
	if (m.value("kind").toString() == "highlight") {
		const QColor base = palette().color(QPalette::Base);
		data->background = ui::blend(base, ui::platformColor(m.value("platform").toString()), 0.22f);
		format.setLeftMargin(2);
	}
	const bool featured = !data->id.isEmpty() && data->id == featuredId_;
	const QColor background = featured ? featuredColor() : data->background;
	if (background.isValid())
		format.setBackground(background);
	QTextBlock block = document()->lastBlock();
	block.setUserData(data);
	QTextCursor cursor(block);
	cursor.mergeBlockFormat(format);
	bar->setValue(atBottom ? bar->maximum() : std::max(0, keep));
	if (!atBottom && !rebuilding_) {
		++unread_;
		updatePill();
	}
	if (!rebuilding_)
		emit contentChanged();
}

QColor ChatView::featuredColor() const
{
	return ui::blend(palette().color(QPalette::Base), palette().color(QPalette::Highlight), 0.5f);
}

void ChatView::setFeatured(const QString &id)
{
	if (id == featuredId_)
		return;
	const QString old = featuredId_;
	featuredId_ = id;
	// Solo cambian los bloques del destacado anterior y del nuevo: sin reconstruir, para no
	// mover la lista si se está leyendo más arriba.
	for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
		const auto *data = dynamic_cast<MessageData *>(block.userData());
		if (!data || data->id.isEmpty())
			continue;
		if (data->id == id)
			setBlockBackground(block, featuredColor());
		else if (data->id == old)
			setBlockBackground(block, data->background);
	}
}

QString ChatView::messageIdAt(const QPoint &pos) const
{
	const QTextBlock block = cursorForPosition(pos).block();
	if (!block.isValid())
		return {};
	// cursorForPosition da el bloque más cercano: hay que estar dentro de él de verdad.
	const QPointF docPos(pos.x() + horizontalScrollBar()->value(), pos.y() + verticalScrollBar()->value());
	if (!document()->documentLayout()->blockBoundingRect(block).contains(docPos))
		return {};
	const auto *data = dynamic_cast<MessageData *>(block.userData());
	return data ? data->id : QString();
}

QJsonObject ChatView::messageAt(const QPoint &pos) const
{
	const QString id = messageIdAt(pos);
	if (id.isEmpty())
		return {};
	for (auto it = messages_.crbegin(); it != messages_.crend(); ++it)
		if (it->value("id").toString() == id)
			return *it;
	return {};
}

void ChatView::contextMenuEvent(QContextMenuEvent *event)
{
	emit menuRequested(messageAt(viewport()->mapFrom(this, event->pos())), event->globalPos());
	event->accept();
}

QMimeData *ChatView::createMimeDataFromSelection() const
{
	auto *data = new QMimeData;
	QString text = textCursor().selection().toPlainText();
	text.remove(QChar::ObjectReplacementCharacter); // emotes and icons
	data->setText(text);
	return data;
}

void ChatView::mousePressEvent(QMouseEvent *event)
{
	pressPos_ = event->position().toPoint();
	QTextBrowser::mousePressEvent(event);
}

void ChatView::mouseReleaseEvent(QMouseEvent *event)
{
	QTextBrowser::mouseReleaseEvent(event);
	if (event->button() != Qt::LeftButton)
		return;
	const QPoint pos = event->position().toPoint();
	// Arrastrar para seleccionar texto o pulsar un enlace no destaca nada.
	if ((pos - pressPos_).manhattanLength() > QApplication::startDragDistance() || textCursor().hasSelection() ||
	    !anchorAt(pos).isEmpty())
		return;
	const QJsonObject message = messageAt(pos);
	if (!message.isEmpty())
		emit messageClicked(message);
}

void ChatView::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_End) {
		scrollToBottom();
		event->accept();
		return;
	}
	if (event->key() == Qt::Key_Escape && !ChatManager::instance().featured().isEmpty()) {
		ChatManager::instance().clearFeatured();
		event->accept();
		return;
	}
	QTextBrowser::keyPressEvent(event);
}

void ChatView::setMessages(const QList<QJsonObject> &messages)
{
	messages_ = messages;
	rebuild();
}

void ChatView::removeMessages(const QJsonObject &deletion)
{
	QSet<QString> ids;
	for (const QJsonValue v : deletion.value("ids").toArray())
		ids.insert(v.toString());
	for (auto it = messages_.begin(); it != messages_.end();) {
		if (ids.contains(it->value("id").toString()) || chat::deletionMatches(deletion, *it)) {
			ids.insert(it->value("id").toString());
			it = messages_.erase(it);
		} else {
			++it;
		}
	}
	ids.remove(QString());
	if (ids.isEmpty())
		return;

	QScrollBar *bar = verticalScrollBar();
	const bool atBottom = bar->value() >= bar->maximum() - 4;
	int keep = bar->value();
	QAbstractTextDocumentLayout *layout = document()->documentLayout();
	QList<QTextBlock> doomed;
	for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
		const auto *data = dynamic_cast<MessageData *>(block.userData());
		if (!data || !ids.contains(data->id))
			continue;
		doomed << block;
		// Content above the viewport shifts up when removed: compensate so the view stays put.
		const QRectF rect = layout->blockBoundingRect(block);
		if (rect.bottom() <= keep)
			keep -= qRound(rect.height());
	}
	if (doomed.isEmpty())
		return;

	QTextCursor cursor(document());
	cursor.beginEditBlock();
	// From the last one so earlier positions stay valid.
	for (auto it = doomed.crbegin(); it != doomed.crend(); ++it) {
		const QTextBlock block = *it;
		const QTextBlock next = block.next();
		if (block.blockNumber() > 0) {
			// Takes the separator before the block too, so no empty line is left behind.
			cursor.setPosition(block.position() - 1);
			cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
			cursor.removeSelectedText();
		} else if (next.isValid()) {
			// Removing the first block keeps its format and data on the merged block: carry the
			// next message's ones over explicitly.
			const auto *nextData = dynamic_cast<MessageData *>(next.userData());
			auto *copy = new MessageData;
			if (nextData) {
				copy->id = nextData->id;
				copy->background = nextData->background;
			}
			const QTextBlockFormat format = next.blockFormat();
			cursor.setPosition(block.position());
			cursor.setPosition(next.position(), QTextCursor::KeepAnchor);
			cursor.removeSelectedText();
			QTextBlock first = document()->firstBlock();
			first.setUserData(copy);
			cursor.setPosition(first.position());
			cursor.setBlockFormat(format);
		} else {
			cursor.select(QTextCursor::Document);
			cursor.removeSelectedText();
			document()->firstBlock().setUserData(nullptr);
		}
	}
	cursor.endEditBlock();
	bar->setValue(atBottom ? bar->maximum() : std::max(0, keep));
	emit contentChanged();
}

void ChatView::setHiddenSources(const QSet<QString> &hidden)
{
	hidden_ = hidden;
	rebuild();
}

void ChatView::setSearch(const QString &query)
{
	if (query == search_)
		return;
	search_ = query;
	rebuild();
	scrollToBottom(); // the newest matches first
}

void ChatView::setShowTimestamps(bool show)
{
	if (show == timestamps_)
		return;
	timestamps_ = show;
	rebuild();
}

int ChatView::shownCount() const
{
	return document()->isEmpty() ? 0 : document()->blockCount();
}

void ChatView::setFontSize(int pointSize)
{
	QFont f = font();
	f.setPointSize(pointSize);
	setFont(f);
	emoteSize_ = qRound(QFontMetricsF(f).height() * 1.45);
	rebuild();
}

void ChatView::rebuild()
{
	QScrollBar *bar = verticalScrollBar();
	const bool atBottom = isAtBottom();
	const int oldValue = bar->value();
	// Reading further up: remember the message at the top of the view and where it was.
	QString anchorId;
	qreal anchorOffset = 0;
	if (!atBottom) {
		const QTextBlock top = cursorForPosition(QPoint(0, 0)).block();
		if (const auto *data = dynamic_cast<MessageData *>(top.userData())) {
			anchorId = data->id;
			anchorOffset = document()->documentLayout()->blockBoundingRect(top).top() - oldValue;
		}
	}

	const QList<QJsonObject> all = messages_;
	messages_.clear();
	rebuilding_ = true;
	clear();
	for (const QJsonObject &m : all)
		addMessage(m);
	rebuilding_ = false;
	emit contentChanged();

	if (atBottom) {
		scrollToBottom();
		return;
	}
	int value = oldValue;
	if (!anchorId.isEmpty())
		for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
			const auto *data = dynamic_cast<MessageData *>(block.userData());
			if (data && data->id == anchorId) {
				value = qRound(document()->documentLayout()->blockBoundingRect(block).top() - anchorOffset);
				break;
			}
		}
	bar->setValue(std::clamp(value, 0, bar->maximum()));
}

// ---------------------------------------------------------------------------------------
// ChatDock

ChatDock::ChatDock(QWidget *parent) : QWidget(parent)
{
	const QJsonObject prefs = loadPrefs();
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
	status_->setAccessibleName(ui::T("Dock.Status"));
	// Monochrome SVG icons tinted with the theme (applyTheme); the tooltip names the action.
	auto button = [&](const QString &icon, const char *tip, auto slot) {
		auto *b = new QToolButton(this);
		b->setToolTip(ui::T(tip));
		b->setAccessibleName(ui::T(tip));
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
	button("refresh", "Dock.Refresh", [] { ChatManager::instance().refresh(); });
	button("clear", "Dock.Clear", [this] { clearWithUndo(); });
	button("settings", "Dock.Settings", [this] { open_multichat_settings(this); });
	root->addLayout(header);

	banner_ = new QWidget(this);
	banner_->setObjectName("multichatBanner");
	banner_->setAttribute(Qt::WA_StyledBackground);
	auto *bannerLayout = new QHBoxLayout(banner_);
	bannerLayout->setContentsMargins(6, 3, 6, 3);
	bannerLayout->setSpacing(4);
	bannerText_ = new QLabel(ui::T("Dock.Multistream"), banner_);
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
	auto *download = new QPushButton(ui::T("Update.Now"), update);
	updateLayout->addWidget(updateText, 1);
	updateLayout->addWidget(download);
	update->hide();
	root->addWidget(update);
	auto showUpdate = [update, updateText, download](const chat::Release &release) {
		if (release.version.isEmpty())
			return;
		updateText->setText(ui::T("Update.Available").arg(release.version));
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
	undoLayout->addWidget(new QLabel(ui::T("Dock.Cleared"), undoBar_), 1);
	auto *undoButton = new QPushButton(ui::T("Dock.Undo"), undoBar_);
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
	searchEdit_->setPlaceholderText(ui::T("Dock.Search.Placeholder"));
	searchEdit_->setAccessibleName(ui::T("Dock.Search"));
	searchEdit_->setClearButtonEnabled(true);
	searchCount_ = new QLabel(searchBar_);
	auto *searchClose = new QToolButton(searchBar_);
	searchClose->setAutoRaise(true);
	searchClose->setIconSize(QSize(14, 14));
	iconButtons_.append({searchClose, "close"});
	searchClose->setToolTip(ui::T("Dock.Search.Close"));
	searchClose->setAccessibleName(ui::T("Dock.Search.Close"));
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
	auto *emptyText = new QLabel(ui::T("Dock.Empty"), empty_);
	emptyText->setWordWrap(true);
	emptyText->setAlignment(Qt::AlignCenter);
	auto *emptyButton = new QPushButton(ui::T("Dock.Configure"), empty_);
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
		menu.addAction(ui::T(featured ? "Dock.Menu.Unfeature" : "Dock.Menu.Feature"), this,
			       [message, featured] {
				       if (featured)
					       ChatManager::instance().clearFeatured();
				       else
					       ChatManager::instance().feature(message);
			       });
		QAction *hide = menu.addAction(ui::T("Dock.Menu.HideMessage"), this,
					       [message] { ChatManager::instance().hideMessage(message); });
		hide->setEnabled(!chat::hideMessage(message).isEmpty());
		QString author = message.value("author").toString();
		author.replace('&', "&&"); // not a mnemonic
		QAction *hideUser = menu.addAction(ui::T("Dock.Menu.HideUser").arg(author), this,
						   [message] { ChatManager::instance().hideUser(message); });
		hideUser->setEnabled(!author.isEmpty());
		menu.addSeparator();
		menu.addAction(ui::T("Dock.Menu.CopyMessage"), this,
			       [message] { QGuiApplication::clipboard()->setText(chat::plainText(message)); });
		menu.addAction(ui::T("Dock.Menu.CopyName"), this, [message] {
			QGuiApplication::clipboard()->setText(message.value("author").toString());
		});
	}
	if (view_->textCursor().hasSelection())
		menu.addAction(ui::T("Dock.Menu.CopySelection"), view_, &ChatView::copy);
	if (!menu.isEmpty())
		menu.addSeparator();
	QAction *search = menu.addAction(ui::T("Dock.Search"), this, &ChatDock::openSearch);
	search->setShortcut(QKeySequence::Find);
	QAction *time = menu.addAction(ui::T("Dock.Menu.ShowTime"));
	time->setCheckable(true);
	time->setChecked(timestamps_);
	connect(time, &QAction::toggled, this, [this](bool on) { setShowTimestamps(on); });
	menu.exec(globalPos);
}

void ChatDock::setShowTimestamps(bool show)
{
	timestamps_ = show;
	view_->setShowTimestamps(show);
	QJsonObject prefs = loadPrefs();
	prefs.insert("timestamps", show);
	savePrefs(prefs);
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
	searchCount_->setText(n == 0 ? ui::T("Dock.Search.None") : ui::T("Dock.Search.Count").arg(n));
}

void ChatDock::changeFont(int delta)
{
	fontSize_ = std::clamp(fontSize_ + delta, 8, 24);
	view_->setFontSize(fontSize_);
	QJsonObject prefs = loadPrefs();
	prefs.insert("fontSize", fontSize_);
	savePrefs(prefs);
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
		status_->setText(QString::fromUtf8("● ") + ui::T("Dock.OnAir") +
				 (service.isEmpty() ? QString() : QStringLiteral(" · ") + service));
		const QString style = QStringLiteral("color: %1; font-weight: bold;").arg(alert.name());
		status_->setStyleSheet(style);
		statusViewers_->setStyleSheet(style);
		setIconLabel(statusEye_, tintedPixmap(QStringLiteral("eye"), alert, 14, dpr), QString::fromUtf8("👁"));
	} else {
		QStringList names;
		for (const QJsonValue p : live)
			names << chat::platformName(p.toString());
		status_->setText(names.isEmpty() ? ui::T("Dock.OffAir") : ui::T("Dock.LiveOn").arg(names.join(", ")));
		status_->setStyleSheet(QString());
		statusViewers_->setStyleSheet(QString());
		setIconLabel(statusEye_, eye, QString::fromUtf8("👁"));
		showViewers = showViewers && !names.isEmpty();
	}
	statusViewers_->setText(showViewers ? ui::formatCount(viewers) : QString());
	statusEye_->setVisible(showViewers);
	statusViewers_->setVisible(showViewers);
	statusEye_->setToolTip(showViewers ? ui::T("Dock.Viewers").arg(ui::formatCount(viewers)) : QString());

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
				QJsonObject prefs = loadPrefs();
				QJsonArray list;
				for (const QString &h : hidden_)
					list.append(h);
				prefs.insert("hidden", list);
				savePrefs(prefs);
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
		const QString statusText = ui::T(("Status." + status).toUtf8().constData());
		QString tip = chat::platformName(platform) + " · " + s.value("channel").toString() + "\n" + statusText;
		const int liveState = s.value("live").toInt();
		tip += " · " + ui::T(liveState == 1 ? "Live.Yes" : liveState == 0 ? "Live.No" : "Live.Unknown");
		if (chipViewers >= 0)
			tip += " · " + ui::T("Dock.Viewers").arg(ui::formatCount(chipViewers));
		if (!s.value("detail").toString().isEmpty())
			tip += "\n" + s.value("detail").toString();
		tip += "\n" + ui::T("Dock.ChipHint");
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
