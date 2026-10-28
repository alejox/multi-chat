#include "ui-common.hpp"

#include <obs-module.h>

#include <QLocale>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace ui {

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString formatCount(int count)
{
	QLocale locale;
	if (count < 1000)
		return locale.toString(count);
	if (count < 1000000)
		return locale.toString(count / 1000.0, 'f', count < 10000 ? 1 : 0) + QStringLiteral(" K");
	return locale.toString(count / 1000000.0, 'f', 1) + QStringLiteral(" M");
}

QColor platformColor(const QString &platform)
{
	if (platform == "twitch")
		return QColor(0x91, 0x46, 0xFF);
	if (platform == "youtube")
		return QColor(0xFF, 0x00, 0x33);
	if (platform == "kick")
		return QColor(0x53, 0xFC, 0x18);
	if (platform == "tiktok")
		return QColor(0xFE, 0x2C, 0x55);
	return QColor(0x88, 0x88, 0x88);
}

QPixmap platformIcon(const QString &platform, int size, qreal dpr)
{
	QPixmap pm(QSize(size, size) * dpr);
	pm.setDevicePixelRatio(dpr);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	p.scale(size / 24.0, size / 24.0); // dibujamos en una rejilla de 24×24
	p.setPen(Qt::NoPen);

	if (platform == "twitch") {
		QPainterPath bubble;
		bubble.moveTo(4.3, 1);
		bubble.lineTo(2.5, 5.6);
		bubble.lineTo(2.5, 20);
		bubble.lineTo(7.5, 20);
		bubble.lineTo(7.5, 23);
		bubble.lineTo(10.3, 23);
		bubble.lineTo(13.3, 20);
		bubble.lineTo(17.4, 20);
		bubble.lineTo(23, 14.4);
		bubble.lineTo(23, 1);
		bubble.closeSubpath();
		p.setBrush(platformColor(platform));
		p.drawPath(bubble);
		p.setBrush(Qt::white);
		p.drawRect(QRectF(5.2, 3.2, 15.5, 11));
		p.setBrush(platformColor(platform));
		p.drawRect(QRectF(15.8, 6.6, 2, 5.8));
		p.drawRect(QRectF(10.4, 6.6, 2, 5.8));
	} else if (platform == "youtube") {
		p.setBrush(platformColor(platform));
		p.drawRoundedRect(QRectF(1, 4.5, 22, 15), 4.5, 4.5);
		QPainterPath play;
		play.moveTo(10, 8.6);
		play.lineTo(15.8, 12);
		play.lineTo(10, 15.4);
		play.closeSubpath();
		p.setBrush(Qt::white);
		p.drawPath(play);
	} else if (platform == "kick") {
		p.setBrush(platformColor(platform));
		p.drawRoundedRect(QRectF(1, 1, 22, 22), 5, 5);
		QPainterPath k;
		k.moveTo(6.5, 5.5);
		k.lineTo(10.1, 5.5);
		k.lineTo(10.1, 9.1);
		k.lineTo(11.9, 9.1);
		k.lineTo(11.9, 7.3);
		k.lineTo(13.7, 7.3);
		k.lineTo(13.7, 5.5);
		k.lineTo(17.3, 5.5);
		k.lineTo(17.3, 11);
		k.lineTo(15.5, 11);
		k.lineTo(15.5, 13);
		k.lineTo(17.3, 13);
		k.lineTo(17.3, 18.5);
		k.lineTo(13.7, 18.5);
		k.lineTo(13.7, 16.7);
		k.lineTo(11.9, 16.7);
		k.lineTo(11.9, 14.9);
		k.lineTo(10.1, 14.9);
		k.lineTo(10.1, 18.5);
		k.lineTo(6.5, 18.5);
		k.closeSubpath();
		p.setBrush(QColor(0x0b, 0x0b, 0x0b));
		p.drawPath(k);
	} else if (platform == "tiktok") {
		p.setBrush(QColor(0x0b, 0x0b, 0x0b));
		p.drawRoundedRect(QRectF(1, 1, 22, 22), 5, 5);
		// La nota musical en cian y rosa desplazados, y blanca encima.
		QPainterPath note;
		note.moveTo(12.2, 5);
		note.lineTo(14.4, 5);
		note.cubicTo(14.8, 7.3, 16.6, 8.6, 18.6, 8.7);
		note.lineTo(18.6, 11);
		note.cubicTo(17, 11, 15.5, 10.4, 14.4, 9.4);
		note.lineTo(14.4, 15.5);
		note.arcTo(QRectF(5.8, 11.2, 8.6, 8.6), 0, -270);
		note.lineTo(10.1, 13.4);
		note.arcTo(QRectF(8, 13.4, 4.2, 4.2), 90, 270);
		note.closeSubpath();
		p.setBrush(QColor(0x25, 0xF4, 0xEE));
		p.drawPath(note.translated(-0.8, -0.8));
		p.setBrush(platformColor(platform));
		p.drawPath(note.translated(0.8, 0.8));
		p.setBrush(Qt::white);
		p.drawPath(note);
	} else {
		p.setBrush(platformColor(platform));
		p.drawEllipse(QRectF(2, 2, 20, 20));
		p.setPen(QPen(Qt::white, 2.4));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QRectF(8, 8, 8, 8));
	}
	return pm;
}

QPixmap avatarPixmap(const QImage &photo, const QString &platform, int size, qreal dpr)
{
	if (photo.isNull())
		return platformIcon(platform, size, dpr);
	QPixmap pm(QSize(size, size) * dpr);
	pm.setDevicePixelRatio(dpr);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	QPainterPath circle;
	circle.addEllipse(QRectF(0, 0, size, size));
	p.save();
	p.setClipPath(circle);
	p.drawImage(QRectF(0, 0, size, size), photo);
	p.restore();
	const int badge = std::max(10, size * 2 / 5);
	p.drawPixmap(QPointF(size - badge, size - badge), platformIcon(platform, badge, dpr));
	return pm;
}

QIcon statusIcon(const QString &platform, const QString &status, int size, qreal dpr)
{
	QPixmap pm = platformIcon(platform, size, dpr);
	QColor dot;
	if (status == "connected")
		dot = QColor(0x3e, 0xcf, 0x6e);
	else if (status == "connecting" || status == "waiting")
		dot = QColor(0xf5, 0xb9, 0x42);
	else if (status == "error")
		dot = QColor(0xff, 0x5c, 0x5c);
	if (dot.isValid()) {
		QPainter p(&pm);
		p.setRenderHint(QPainter::Antialiasing);
		const qreal d = size * 0.42;
		p.setPen(QPen(QColor(0, 0, 0, 160), 1.2));
		p.setBrush(dot);
		p.drawEllipse(QRectF(size - d - 0.5, size - d - 0.5, d, d));
	}
	return QIcon(pm);
}

bool badgeStyle(const QString &badge, QString &label, QColor &background)
{
	if (badge == "owner" || badge == "staff") {
		label = badge == "owner" ? "HOST" : "STAFF";
		background = QColor(0xe5, 0x48, 0x4d);
	} else if (badge == "moderator") {
		label = "MOD";
		background = QColor(0x2f, 0x9e, 0x58);
	} else if (badge == "vip") {
		label = "VIP";
		background = QColor(0xd6, 0x40, 0x9f);
	} else if (badge == "subscriber") {
		label = "SUB";
		background = QColor(0x6e, 0x56, 0xcf);
	} else if (badge == "member") {
		label = T("Badge.Member");
		background = QColor(0x6e, 0x56, 0xcf);
	} else if (badge == "verified") {
		label = QString::fromUtf8("✓");
		background = QColor(0x5a, 0x60, 0x70);
	} else {
		return false;
	}
	return true;
}

QColor readableColor(const QString &hex, const QString &name, const QColor &background)
{
	static const QColor fallback[] = {"#FF6B6B", "#4FC3F7", "#FFB300", "#66BB6A", "#BA68C8",
					  "#FF8A65", "#26C6DA", "#F06292", "#9CCC65", "#9575CD"};
	QColor c(hex);
	if (!c.isValid()) {
		const size_t h = qHash(name);
		c = fallback[h % (sizeof(fallback) / sizeof(fallback[0]))];
	}
	const bool darkBg = background.lightnessF() < 0.5;
	const float l = float(c.lightnessF());
	if (darkBg && l < 0.55f)
		c = QColor::fromHslF(c.hslHueF(), c.hslSaturationF(), 0.55f + (0.55f - l) * 0.3f);
	else if (!darkBg && l > 0.45f)
		c = QColor::fromHslF(c.hslHueF(), c.hslSaturationF(), 0.38f);
	return c;
}

} // namespace ui
