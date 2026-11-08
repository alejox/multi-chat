// Sizes of the viewers and clock boxes, and fitting the chat ticker to the canvas and scenes.

#include "source-internal.hpp"

namespace overlay_source {

// Canales que muestra una fuente de espectadores: los que están en vivo o, para colocarla
// sin estar en directo, unos de ejemplo.
QList<ViewerItem> viewerItems(const QJsonObject &config, const QJsonArray &channels)
{
	const QJsonArray platforms = config.value("platforms").toArray();
	QList<ViewerItem> items;
	for (const QJsonValue v : channels) {
		const QJsonObject src = v.toObject();
		const QString platform = src.value("platform").toString();
		if (src.value("live").toInt() == 1 && platform != "demo" && platforms.contains(platform))
			items.append({platform, src.value("channel").toString(), src.value("viewers").toInt(-1)});
	}
	if (items.isEmpty() && sampleMessages && !streaming) {
		const ViewerItem samples[] = {{"twitch", "micanal", 1234},
					      {"youtube", "@micanal", 856},
					      {"kick", "micanal", 312},
					      {"tiktok", "@micanal", 2045},
					      {"starvios", "micanal", 148}};
		for (const ViewerItem &sample : samples)
			if (platforms.contains(sample.platform))
				items.append(sample);
	}
	return items;
}

// El total se muestra si hay más de un canal y al menos uno con espectadores conocidos.
int viewerTotal(const QJsonObject &config, const QList<ViewerItem> &items)
{
	if (!config.value("total").toBool() || items.size() < 2)
		return -1;
	int total = -1;
	for (const ViewerItem &item : items)
		if (item.viewers >= 0)
			total = std::max(total, 0) + item.viewers;
	return total;
}

// Mismo formato que viewers.html: 1.234 · 12.5K · 1.2M
QString formatViewers(int n)
{
	if (n < 0)
		return QStringLiteral("—");
	if (n < 10000)
		return QLocale().toString(n);
	if (n < 1000000)
		return QString::number(n / 1000.0, 'f', n < 100000 ? 1 : 0).remove(QRegularExpression("\\.0$")) + 'K';
	return QString::number(n / 1000000.0, 'f', 1).remove(QRegularExpression("\\.0$")) + 'M';
}

// Font used to measure a box: the chosen family (if any) before the default stack, with the
// chosen weight. The page then scales the box, so an approximation is enough.
QFont measureFont(const QJsonObject &config, double em, int defaultWeight)
{
	QFont font;
	QStringList families{"Segoe UI", ".AppleSystemUIFont", "Helvetica Neue", "Roboto"};
	const QString face = config.value("fontFace").toString().trimmed();
	if (!face.isEmpty())
		families.prepend(face);
	font.setFamilies(families);
	font.setPixelSize(std::max(1, int(em)));
	font.setWeight(QFont::Weight(std::clamp(config.value("textWeight").toInt(defaultWeight), 100, 900)));
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	font.setFeature(QFont::Tag("tnum"), 1); // cifras de ancho fijo, como font-variant-numeric
#endif
	return font;
}

// Lo que suma el borde del recuadro a cada dimensión (los estilos con borde propio usan hasta 2 px).
double boxBorder(const QJsonObject &config)
{
	if (!config.value("background").toBool())
		return 0;
	return 2.0 * std::max(2, config.value("bgBorderWidth").toInt());
}

// Tamaño del recuadro con las medidas de viewers.html (en em = tamaño de letra). La página
// luego escala el recuadro para que llene la fuente justo, así que basta con aproximarlo.
QSize viewerBoxSize(const QJsonObject &config, const QList<ViewerItem> &items, int total)
{
	const double em = std::max(1, config.value("size").toInt(28));
	const QFont font = measureFont(config, em, 700);
	QFont small = font;
	small.setPixelSize(std::max(1, int(em * 0.6)));
	small.setWeight(QFont::DemiBold);
	const QFontMetricsF big(font), little(small);
	const bool names = config.value("names").toBool();
	const bool column = config.value("layout").toString() == "column";
	const double padX = 2.0 * config.value("padX").toInt(17), padY = 2.0 * config.value("padY").toInt(10);

	QList<double> widths;
	for (const ViewerItem &item : items)
		widths << em * 1.45 + big.horizontalAdvance(formatViewers(item.viewers)) +
				  (names ? em * 0.35 + little.horizontalAdvance(item.name) : 0);
	const double totalWidth = total >= 0 ? em * 1.45 + big.horizontalAdvance(formatViewers(total)) : 0;

	double w, h;
	if (column) {
		w = totalWidth;
		for (double x : widths)
			w = std::max(w, x);
		w += padX;
		h = padY + double(items.size()) * em * 1.2 + double(items.size() - 1) * em * 0.3;
		if (total >= 0)
			h += em * 1.75 + 2;
	} else {
		w = padX + em * 0.9 * double(items.size() - 1);
		for (double x : widths)
			w += x;
		if (total >= 0)
			w += em * 1.8 + 2 + totalWidth;
		h = em * 1.2 + padY;
	}
	const double border = boxBorder(config);
	return QSize(std::min(4096, int(std::ceil(w + border))), std::min(4096, int(std::ceil(h + border))));
}

QJsonObject viewersPayload(const QList<ViewerItem> &items, int total)
{
	QJsonArray list;
	for (const ViewerItem &item : items)
		list.append(QJsonObject{{"platform", item.platform},
					{"key", item.platform + ':' + item.name},
					{"name", item.name},
					{"viewers", item.viewers}});
	return QJsonObject{{"type", "viewers"}, {"items", list}, {"total", total}};
}

void resizeBrowser(OverlaySource *s);

// Manda los espectadores a la página y ajusta la fuente al tamaño del recuadro.
// Si no hay nada que mostrar se queda con el tamaño anterior para no saltar.
void refreshViewers(OverlaySource *s)
{
	const QJsonObject config = configOf(s);
	QJsonArray channels;
	{
		std::lock_guard<std::mutex> lock(sourcesMutex);
		channels = lastSources;
	}
	const QList<ViewerItem> items = viewerItems(config, channels);
	const int total = viewerTotal(config, items);
	bool sized;
	{
		std::lock_guard<std::mutex> lock(s->mutex);
		sized = s->sized;
	}
	// Sin nada que mostrar la primera vez, el tamaño de cuatro canales de ejemplo.
	const QList<ViewerItem> measured =
		!items.isEmpty() || sized ? items
					  : QList<ViewerItem>{{"twitch", "micanal", 1234}, {"youtube", "@micanal", 856},
							      {"kick", "micanal", 312}, {"tiktok", "@micanal", 2045},
							      {"starvios", "micanal", 148}};
	if (!measured.isEmpty()) {
		{
			std::lock_guard<std::mutex> lock(s->mutex);
			s->sized = true;
		}
		const QSize size =
			viewerBoxSize(config, measured, items.isEmpty() ? viewerTotal(config, measured) : total);
		if (uint32_t(size.width()) != s->width || uint32_t(size.height()) != s->height) {
			s->width = uint32_t(size.width());
			s->height = uint32_t(size.height());
			resizeBrowser(s);
		}
	}
	send(s, viewersPayload(items, total));
}

// ---- Fuente de fecha y hora ----

// Tamaño del recuadro de clock.html con un texto de ejemplo del ancho máximo (la página luego
// escala el recuadro para llenar la fuente justo).
QSize clockBoxSize(const QJsonObject &c)
{
	const double em = std::max(1, c.value("size").toInt(32));
	const QFont font = measureFont(c, em, 800);
	const QDateTime sample(QDate(2026, 12, 28), QTime(23, 58, 58));
	QStringList parts;
	if (c.value("date").toBool())
		parts << QLocale().toString(sample.date(), "dd MMM yyyy").remove('.').toUpper();
	if (c.value("time").toBool())
		parts << QLocale()
				 .toString(sample.time(), QString(c.value("hour24").toBool() ? "HH:mm" : "hh:mm") +
								  (c.value("seconds").toBool() ? ":ss" : "") +
								  (c.value("hour24").toBool() ? "" : " AP"))
				 .toUpper();
	const QString text = parts.join(' ');
	double w = 2.0 * c.value("padX").toInt(19) + QFontMetricsF(font).horizontalAdvance(text) + em * 0.02 * double(text.size());
	if (c.value("bar").toBool())
		w += em * 0.63;
	const double border = boxBorder(c);
	return QSize(std::min(4096, int(std::ceil(w + border))), int(std::ceil(em * 1.2 + 2.0 * c.value("padY").toInt(11) + border)));
}

void refreshClock(OverlaySource *s)
{
	const QSize size = clockBoxSize(configOf(s));
	if (uint32_t(size.width()) != s->width || uint32_t(size.height()) != s->height) {
		s->width = uint32_t(size.width());
		s->height = uint32_t(size.height());
		resizeBrowser(s);
	}
}

// Tamaño de la cinta horizontal: el ancho del lienzo y el alto justo de una línea. La línea
// mide el interlineado (1,35 por defecto) × la letra (+10 px de relleno con fondo) y se deja un margen pequeño arriba y abajo.
uint32_t canvasWidth()
{
	obs_video_info ovi;
	return obs_get_video_info(&ovi) && ovi.base_width ? ovi.base_width : 1920;
}

uint32_t tickerHeight(const QJsonObject &config)
{
	const double size = config.value("size").toInt(22);
	// Con fondo: el relleno del mensaje y el borde (los estilos con borde propio usan hasta 2 px).
	const double border = std::max(2, config.value("bgBorderWidth").toInt());
	const double line = size * config.value("lineHeight").toInt(135) / 100.0 + (config.value("background").toBool()
						   ? 2.0 * config.value("msgPadY").toInt() + 2 * border
						   : 0);
	return uint32_t(std::max(10.0, std::ceil(line + 2.0 * config.value("padY").toInt())));
}

// Cinta horizontal con "ajustar al lienzo": su tamaño lo pone el plugin.
bool fitsCanvas(Kind kind, const QJsonObject &config)
{
	return kind == Kind::Chat && config.value("layout").toString() == "horizontal" && config.value("fit").toBool();
}

// Ancho de la cinta ajustada: el elegido o, con 0, el del lienzo.
uint32_t tickerWidth(const QJsonObject &config)
{
	const int width = config.value("tickerWidth").toInt();
	return width > 0 ? uint32_t(width) : canvasWidth();
}

// Al pasar a la cinta ajustada al lienzo, quita en las escenas la escala, el recorte y los
// límites que tuviera la fuente y la lleva al borde izquierdo, para que ocupe el lienzo justo.
void fitSceneItems(obs_scene_t *scene, OverlaySource *s);

bool fitSceneItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *s = static_cast<OverlaySource *>(param);
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, fitSceneItem, param);
	if (obs_sceneitem_get_source(item) != s->self)
		return true;
	obs_video_info ovi;
	const float canvasHeight = obs_get_video_info(&ovi) ? float(ovi.base_height) : 1080.0f;
	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	obs_sceneitem_set_rot(item, 0.0f);
	vec2 scale;
	vec2_set(&scale, 1.0f, 1.0f);
	obs_sceneitem_set_scale(item, &scale);
	const obs_sceneitem_crop crop = {0, 0, 0, 0};
	obs_sceneitem_set_crop(item, &crop);
	// Arriba a la izquierda: x = 0 y la misma altura, sin salirse del lienzo.
	vec2 pos;
	obs_sceneitem_get_pos(item, &pos);
	const uint32_t align = obs_sceneitem_get_alignment(item);
	const float h = float(s->height.load());
	if (align & OBS_ALIGN_BOTTOM)
		pos.y -= h;
	else if (!(align & OBS_ALIGN_TOP))
		pos.y -= h / 2;
	pos.x = 0.0f;
	pos.y = std::clamp(pos.y, 0.0f, std::max(0.0f, canvasHeight - h));
	obs_sceneitem_set_alignment(item, OBS_ALIGN_TOP | OBS_ALIGN_LEFT);
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_defer_update_end(item);
	return true;
}

void fitSceneItems(obs_scene_t *scene, OverlaySource *s)
{
	obs_scene_enum_items(scene, fitSceneItem, s);
}

void fitAllScenes(OverlaySource *s)
{
	obs_enum_scenes(
		[](void *param, obs_source_t *source) {
			if (obs_scene_t *scene = obs_scene_from_source(source))
				fitSceneItems(scene, static_cast<OverlaySource *>(param));
			else if (obs_scene_t *group = obs_group_from_source(source))
				fitSceneItems(group, static_cast<OverlaySource *>(param));
			return true;
		},
		s);
}

obs_data_t *browserSettings(const OverlaySource *s)
{
	obs_data_t *bs = obs_data_create();
	// En Windows obs_module_file puede dar una ruta relativa: la fuente de navegador
	// necesita la absoluta.
	char *page = obs_module_file(s->kind == Kind::Viewers    ? "overlay/viewers.html"
				     : s->kind == Kind::Clock    ? "overlay/clock.html"
				     : s->kind == Kind::Featured ? "overlay/featured.html"
								 : "overlay/index.html");
	const QString path = page ? QFileInfo(QString::fromUtf8(page)).absoluteFilePath() : QString();
	bfree(page);
	obs_data_set_bool(bs, "is_local_file", true);
	obs_data_set_string(bs, "local_file", path.toUtf8().constData());
	obs_data_set_int(bs, "width", s->width);
	obs_data_set_int(bs, "height", s->height);
	obs_data_set_int(bs, "fps", 30);
	obs_data_set_bool(bs, "shutdown", false);
	obs_data_set_bool(bs, "reroute_audio", false);
	obs_data_set_string(bs, "css", "");
	return bs;
}

void resizeBrowser(OverlaySource *s)
{
	if (!s->browser)
		return;
	obs_data_t *bs = browserSettings(s);
	obs_source_update(s->browser, bs);
	obs_data_release(bs);
}

} // namespace overlay_source
