#include "support-dialog.hpp"
#include "../i18n.hpp"

#include <QDesktopServices>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const char *const kKofiUrl = "https://ko-fi.com/alejoxcode";

} // namespace

void open_support_dialog(QWidget *parent)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(i18n::tr("Support.Link"));
	dialog.setMinimumWidth(380);
	auto *layout = new QVBoxLayout(&dialog);
	layout->setContentsMargins(22, 20, 22, 18);
	layout->setSpacing(12);

	auto *title = new QLabel(QStringLiteral("☕  ") + i18n::tr("Support.Title"), &dialog);
	QFont titleFont = title->font();
	titleFont.setBold(true);
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
	title->setFont(titleFont);
	title->setWordWrap(true);
	layout->addWidget(title);

	for (const char *key : {"Support.Free", "Support.Why", "Support.NoPressure"}) {
		auto *text = new QLabel(i18n::tr(key), &dialog);
		text->setWordWrap(true);
		layout->addWidget(text);
	}
	layout->addSpacing(4);

	auto *kofi = new QPushButton(i18n::tr("Support.Kofi"), &dialog);
	kofi->setDefault(true);
	kofi->setCursor(Qt::PointingHandCursor);
	kofi->setMinimumHeight(34);
	kofi->setStyleSheet("QPushButton { background-color: palette(highlight); color: palette(highlighted-text); "
			    "font-weight: bold; border: none; border-radius: 6px; padding: 6px 12px; }");
	auto *later = new QPushButton(i18n::tr("Support.Continue"), &dialog);
	later->setMinimumHeight(34);
	layout->addWidget(kofi);
	layout->addWidget(later);

	QObject::connect(kofi, &QPushButton::clicked, &dialog, [&dialog] {
		QDesktopServices::openUrl(QUrl(QString::fromLatin1(kKofiUrl)));
		dialog.accept();
	});
	QObject::connect(later, &QPushButton::clicked, &dialog, &QDialog::reject);
	dialog.exec();
}
