#include "ui-common.hpp"
#include "../i18n.hpp"
#include "../platforms-registry.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QLocale>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cstring>

namespace ui {

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
	const platforms::Platform *p = platforms::find(platform);
	return QColor(QRgb(p ? p->color : platforms::kUnknownColor));
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
	} else if (platform == "starvios") {
		p.setBrush(platformColor(platform));
		p.drawRoundedRect(QRectF(1, 1, 22, 22), 5, 5);
		QPainterPath star;
		star.moveTo(12, 5.2);
		star.lineTo(14.1, 9.6);
		star.lineTo(18.9, 10.2);
		star.lineTo(15.4, 13.5);
		star.lineTo(16.3, 18.3);
		star.lineTo(12, 16);
		star.lineTo(7.7, 18.3);
		star.lineTo(8.6, 13.5);
		star.lineTo(5.1, 10.2);
		star.lineTo(9.9, 9.6);
		star.closeSubpath();
		p.setBrush(Qt::white);
		p.drawPath(star);
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
		label = i18n::tr("Badge.Member");
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

QColor alertColor(const QColor &background)
{
	return readableColor(QStringLiteral("#ff5c5c"), QString(), background);
}

QColor blend(const QColor &from, const QColor &to, float t)
{
	auto mix = [t](float x, float y) {
		return x + (y - x) * t;
	};
	return QColor::fromRgbF(mix(float(from.redF()), float(to.redF())), mix(float(from.greenF()), float(to.greenF())),
				mix(float(from.blueF()), float(to.blueF())));
}

QIcon themedIcon(const QString &name, const QColor &color, int size, qreal dpr)
{
	char *file = obs_module_file(("icons/" + name + ".svg").toUtf8().constData());
	if (!file)
		return {};
	// QIcon reads SVG through Qt's icon engine plugin, which OBS ships for its own icons.
	const QIcon source(QString::fromUtf8(file));
	bfree(file);
	const QPixmap shape = source.pixmap(QSize(size, size), dpr);
	if (shape.isNull())
		return {};
	auto paint = [&shape, dpr](const QColor &c) {
		QImage image = shape.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(1.0);
		QPainter p(&image);
		// Keep the shape, take the color.
		p.setCompositionMode(QPainter::CompositionMode_SourceIn);
		p.fillRect(image.rect(), c);
		p.end();
		image.setDevicePixelRatio(dpr);
		return QPixmap::fromImage(image);
	};
	QIcon icon;
	icon.addPixmap(paint(color), QIcon::Normal);
	QColor faded = color;
	faded.setAlphaF(0.4f);
	icon.addPixmap(paint(faded), QIcon::Disabled);
	return icon;
}

QString streamingServiceName()
{
	obs_service_t *service = obs_frontend_get_streaming_service();
	if (!service)
		return {};
	obs_data_t *settings = obs_service_get_settings(service);
	QString name = QString::fromUtf8(obs_data_get_string(settings, "service"));
	if (name.isEmpty())
		name = QString::fromUtf8(obs_service_get_display_name(obs_service_get_type(service)));
	obs_data_release(settings);
	return name;
}

// ---- Scenes ----

namespace {

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
			const char *itemId = obs_source_get_unversioned_id(obs_sceneitem_get_source(item));
			if (itemId && std::strcmp(itemId, s->id) == 0)
				s->found = true;
			return !s->found;
		},
		&search);
	return search.found;
}

} // namespace

QString editedSceneName()
{
	obs_source_t *scene = editedScene();
	const QString name = scene ? QString::fromUtf8(obs_source_get_name(scene)) : QString();
	obs_source_release(scene);
	return name;
}

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
		const char *existingId = obs_source_get_unversioned_id(existing);
		if (existingId && std::strcmp(existingId, id) == 0) {
			source = existing; // keeps the reference
			break;
		}
		obs_source_release(existing);
		name = base + QStringLiteral(" %1").arg(n);
	}
	if (!source) {
		// A versioned type (the voice) is created as its latest version.
		const char *latest = obs_get_latest_input_type_id(id);
		source = obs_source_create(latest ? latest : id, name.toUtf8().constData(), nullptr, nullptr);
	}
	AddResult result = AddResult::Failed;
	if (source && obs_scene_add(scene, source))
		result = AddResult::Added;
	obs_source_release(source);
	obs_source_release(sceneSource);
	return result;
}

QStringList addMultichatSources(bool chat, bool featured)
{
	QStringList lines;
	auto add = [&lines](const char *id) {
		QString scene;
		const QString name = QString::fromUtf8(obs_source_get_display_name(id));
		switch (addToCurrentScene(id, scene)) {
		case AddResult::Added:
			lines << i18n::tr("Wizard.Sources.Added").arg(name, scene);
			break;
		case AddResult::AlreadyThere:
			lines << i18n::tr("Wizard.Sources.AlreadyThere").arg(name, scene);
			break;
		case AddResult::NoScene:
			lines << i18n::tr("Wizard.Sources.NoScene");
			break;
		case AddResult::Failed:
			lines << i18n::tr("Wizard.Sources.Failed").arg(name);
			break;
		}
	};
	if (chat)
		add("multichat_overlay");
	if (featured)
		add("multichat_featured");
	lines.removeDuplicates();
	return lines;
}

} // namespace ui
