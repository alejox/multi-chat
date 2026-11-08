// Viewers and clock boxes (measured on the UI thread), and fitting the chat ticker to the
// canvas and scenes. The measurement itself lives in box-measure.cpp.

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

void refreshClock(OverlaySource *s)
{
	const QSize size = clockBoxSize(configOf(s));
	if (uint32_t(size.width()) != s->width || uint32_t(size.height()) != s->height) {
		s->width = uint32_t(size.width());
		s->height = uint32_t(size.height());
		resizeBrowser(s);
	}
}

// The viewers and clock boxes are measured with QFont, which is only safe on the UI thread.
// OBS may call create/update from any thread: from there the measurement (and the resize
// after it) is queued to the UI thread, holding only a weak reference to the source. If the
// source is gone by then, nothing happens.
void requestMeasure(OverlaySource *s)
{
	if (s->kind != Kind::Viewers && s->kind != Kind::Clock)
		return;
	QCoreApplication *app = QCoreApplication::instance();
	if (!app)
		return;
	if (QThread::currentThread() == app->thread()) {
		if (s->kind == Kind::Viewers)
			refreshViewers(s);
		else
			refreshClock(s);
		return;
	}
	obs_weak_source_t *weak = obs_source_get_weak_source(s->self);
	if (!weak)
		return;
	QMetaObject::invokeMethod(
		app,
		[weak] {
			obs_source_t *ref = obs_weak_source_get_source(weak);
			obs_weak_source_release(weak);
			if (!ref)
				return;
			// While the reference is held OBS cannot destroy the source, so the matching
			// OverlaySource stays valid.
			forEachSource([ref](OverlaySource *target) {
				if (target->self != ref)
					return;
				if (target->kind == Kind::Viewers)
					refreshViewers(target);
				else if (target->kind == Kind::Clock)
					refreshClock(target);
			});
			obs_source_release(ref);
		},
		Qt::QueuedConnection);
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
