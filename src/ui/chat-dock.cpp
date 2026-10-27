#include "chat-dock.hpp"
#include "flow-layout.hpp"
#include "image-cache.hpp"
#include "ui-common.hpp"
#include "../chat-manager.hpp"
#include "../chat/message.hpp"

#include <obs-module.h>

#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

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

QColor blend(const QColor &a, const QColor &b, qreal t)
{
	return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
				a.blueF() + (b.blueF() - a.blueF()) * t);
}

} // namespace

// ---------------------------------------------------------------------------------------
// ChatView

ChatView::ChatView(QWidget *parent) : QTextBrowser(parent)
{
	setOpenLinks(false);
	setOpenExternalLinks(false);
	setFrameShape(QFrame::NoFrame);
	setUndoRedoEnabled(false);
	document()->setMaximumBlockCount(kMaxMessages);
	document()->setDocumentMargin(4);

	connect(&ImageCache::instance(), &ImageCache::ready, this, [this](const QString &url) {
		QScrollBar *bar = verticalScrollBar();
		const bool atBottom = bar->value() >= bar->maximum() - 4;
		document()->addResource(QTextDocument::ImageResource, QUrl(url), ImageCache::instance().get(url));
		document()->markContentsDirty(0, document()->characterCount());
		if (atBottom)
			bar->setValue(bar->maximum());
	});
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
	const QColor muted = blend(palette().color(QPalette::Text), base, 0.45);
	const QString platform = m.value("platform").toString();
	const int icon = qRound(emoteSize_ * 0.75);
	QString out;

	const QString system = m.value("system").toString();
	if (!system.isEmpty())
		out += QStringLiteral("<span style=\"color:%1; font-style:italic;\">%2</span><br>")
			       .arg(muted.name(), system.toHtmlEscaped());

	out += QStringLiteral("<img src=\"mc-icon:%1\" width=\"%2\" height=\"%2\">&nbsp;").arg(platform).arg(icon);

	for (const QJsonValue &b : m.value("badges").toArray()) {
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
		for (const QJsonValue &v : parts) {
			const QJsonObject p = v.toObject();
			if (p.value("type").toString() == "emote")
				out += QStringLiteral("<img src=\"%1\" height=\"%2\" title=\"%3\">")
					       .arg(p.value("url").toString().toHtmlEscaped())
					       .arg(emoteSize_)
					       .arg(p.value("name").toString().toHtmlEscaped());
			else
				out += p.value("text").toString().toHtmlEscaped();
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
	if (hidden_.contains(m.value("source").toString()))
		return;

	QScrollBar *bar = verticalScrollBar();
	const bool atBottom = bar->value() >= bar->maximum() - 4;
	const int keep = bar->value();
	append(html(m)); // QTextEdit::append sigue abajo solo si ya estaba abajo

	QTextBlockFormat format;
	format.setTopMargin(2);
	format.setBottomMargin(2);
	if (m.value("kind").toString() == "highlight") {
		const QColor base = palette().color(QPalette::Base);
		format.setBackground(blend(base, ui::platformColor(m.value("platform").toString()), 0.22));
		format.setLeftMargin(2);
	}
	QTextCursor cursor(document()->lastBlock());
	cursor.mergeBlockFormat(format);
	bar->setValue(atBottom ? bar->maximum() : keep);
}

void ChatView::setMessages(const QList<QJsonObject> &messages)
{
	messages_ = messages;
	rebuild();
}

void ChatView::setHiddenSources(const QSet<QString> &hidden)
{
	hidden_ = hidden;
	rebuild();
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
	const QList<QJsonObject> all = messages_;
	messages_.clear();
	clear();
	for (const QJsonObject &m : all)
		addMessage(m);
	verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

// ---------------------------------------------------------------------------------------
// ChatDock

ChatDock::ChatDock(QWidget *parent) : QWidget(parent)
{
	const QJsonObject prefs = loadPrefs();
	fontSize_ = prefs.value("fontSize").toInt(11);
	for (const QJsonValue &v : prefs.value("hidden").toArray())
		hidden_.insert(v.toString());

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);

	auto *header = new QHBoxLayout();
	header->setSpacing(2);
	status_ = new QLabel(this);
	status_->setTextFormat(Qt::PlainText);
	header->addWidget(status_, 1);
	auto button = [&](const QString &text, const char *tip, auto slot) {
		auto *b = new QToolButton(this);
		b->setText(text);
		b->setToolTip(ui::T(tip));
		b->setAutoRaise(true);
		connect(b, &QToolButton::clicked, this, slot);
		header->addWidget(b);
		return b;
	};
	button(QStringLiteral("A−"), "Dock.Smaller", [this] { changeFont(-1); });
	button(QStringLiteral("A+"), "Dock.Bigger", [this] { changeFont(1); });
	button(QString::fromUtf8("⟳"), "Dock.Refresh", [] { ChatManager::instance().refresh(); });
	button(QString::fromUtf8("🗑"), "Dock.Clear", [] { ChatManager::instance().clear(); });
	button(QString::fromUtf8("⚙"), "Dock.Settings", [this] { open_multichat_settings(this); });
	root->addLayout(header);

	banner_ = new QWidget(this);
	banner_->setObjectName("multichatBanner");
	banner_->setAttribute(Qt::WA_StyledBackground);
	banner_->setStyleSheet("#multichatBanner { background-color: rgba(145, 70, 255, 70); border-radius: 4px; }");
	auto *bannerLayout = new QHBoxLayout(banner_);
	bannerLayout->setContentsMargins(6, 3, 6, 3);
	bannerLayout->setSpacing(4);
	bannerText_ = new QLabel(ui::T("Dock.Multistream"), banner_);
	QFont bold = bannerText_->font();
	bold.setBold(true);
	bannerText_->setFont(bold);
	bannerLayout->addWidget(bannerText_);
	for (int i = 0; i < 3; ++i) {
		auto *icon = new QLabel(banner_);
		bannerLayout->addWidget(icon);
		bannerIcons_ << icon;
	}
	bannerLayout->addStretch(1);
	banner_->hide();
	root->addWidget(banner_);

	chips_ = new QWidget(this);
	chipsLayout_ = new FlowLayout(chips_, 4);
	root->addWidget(chips_);

	errors_ = new QLabel(this);
	errors_->setWordWrap(true);
	errors_->setStyleSheet("color: #ff6b6b;");
	errors_->hide();
	root->addWidget(errors_);

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

	ChatManager &manager = ChatManager::instance();
	QList<QJsonObject> history;
	for (const QJsonValue &v : manager.history())
		history << v.toObject();
	view_->setMessages(history);
	connect(&manager, &ChatManager::message, view_, &ChatView::addMessage);
	connect(&manager, &ChatManager::cleared, view_, [this] { view_->setMessages({}); });
	connect(&manager, &ChatManager::stateChanged, this, &ChatDock::updateState);
	updateState(manager.state());
}

void ChatDock::changeFont(int delta)
{
	fontSize_ = std::clamp(fontSize_ + delta, 8, 24);
	view_->setFontSize(fontSize_);
	QJsonObject prefs = loadPrefs();
	prefs.insert("fontSize", fontSize_);
	savePrefs(prefs);
}

void ChatDock::updateState(const QJsonObject &state)
{
	const QJsonArray sources = state.value("sources").toArray();
	const QJsonArray live = state.value("livePlatforms").toArray();
	const qreal dpr = devicePixelRatioF();

	// Estado general.
	if (state.value("streaming").toBool()) {
		const QString service = state.value("service").toString();
		status_->setText(QString::fromUtf8("● ") + ui::T("Dock.OnAir") +
				 (service.isEmpty() ? QString() : QStringLiteral(" · ") + service));
		status_->setStyleSheet("color: #ff4d4d; font-weight: bold;");
	} else {
		QStringList names;
		for (const QJsonValue &p : live)
			names << chat::platformName(p.toString());
		status_->setText(names.isEmpty() ? ui::T("Dock.OffAir") : ui::T("Dock.LiveOn").arg(names.join(", ")));
		status_->setStyleSheet(QString());
	}

	// Aviso de multistream con los logos de las plataformas en vivo.
	const bool multi = state.value("multistream").toBool();
	banner_->setVisible(multi);
	for (int i = 0; i < bannerIcons_.size(); ++i) {
		const bool has = i < live.size();
		bannerIcons_[i]->setVisible(has);
		if (has)
			bannerIcons_[i]->setPixmap(ui::platformIcon(live[i].toString(), 16, dpr));
	}

	// Un botón por chat: clic para ocultar o mostrar sus mensajes.
	while (QLayoutItem *item = chipsLayout_->takeAt(0)) {
		delete item->widget();
		delete item;
	}
	for (const QJsonValue &v : sources) {
		const QJsonObject s = v.toObject();
		const QString key = s.value("key").toString();
		const QString platform = s.value("platform").toString();
		const QString status = s.value("status").toString();
		auto *chip = new QToolButton(chips_);
		chip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		chip->setIcon(ui::statusIcon(platform, status, 16, dpr));
		chip->setIconSize(QSize(16, 16));
		chip->setText(s.value("channel").toString() +
			      (s.value("live").toInt() == 1 ? QString::fromUtf8("  🔴") : QString()));
		chip->setCheckable(true);
		chip->setChecked(!hidden_.contains(key));
		QString tip = chat::platformName(platform) + " · " + s.value("channel").toString() + "\n" +
			      ui::T(("Status." + status).toUtf8().constData());
		const int liveState = s.value("live").toInt();
		tip += " · " + ui::T(liveState == 1 ? "Live.Yes" : liveState == 0 ? "Live.No" : "Live.Unknown");
		if (!s.value("detail").toString().isEmpty())
			tip += "\n" + s.value("detail").toString();
		tip += "\n" + ui::T("Dock.ChipHint");
		chip->setToolTip(tip);
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
		chipsLayout_->addWidget(chip);
	}
	chips_->setVisible(!sources.isEmpty());

	QStringList errors;
	for (const QJsonValue &e : state.value("errors").toArray())
		errors << e.toString();
	errors_->setText(errors.join("\n"));
	errors_->setVisible(!errors.isEmpty());

	empty_->setVisible(sources.isEmpty());
	view_->setVisible(!sources.isEmpty());
}
