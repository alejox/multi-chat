#include "chat-view.hpp"
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

namespace {

// Same limit as the manager's history, so what the dock shows can always be featured or restored.
constexpr int kMaxMessages = chat::kHistoryLimit;

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
	viewport()->setToolTip(i18n::tr("Dock.FeatureHint"));

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
	const QString text = i18n::tr("Dock.NewMessages").arg(unread_);
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
