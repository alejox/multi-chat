// Fuentes "Multichat" (el chat en la escena), "Multichat (espectadores)", "Multichat (fecha y
// hora)" y "Multichat (destacado)". Por dentro son fuentes de navegador privadas con
// data/overlay/index.html, viewers.html, clock.html y featured.html; los mensajes, los
// espectadores y el mensaje destacado les llegan con javascript_event.

#include "overlay-source.hpp"
#include "chat-manager.hpp"
#include "overlay-presets.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QObject>
#include <QRegularExpression>
#include <QSize>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

// Chat, Viewers, Clock or Featured (the same enum the pure preset tables use).
using Kind = overlay::SourceKind;

struct OverlaySource {
	Kind kind = Kind::Chat;
	obs_source_t *self = nullptr;
	obs_source_t *browser = nullptr;
	// Guarda config y sized: ov_update puede llegar desde cualquier hilo mientras el temporizador
	// de la interfaz los lee. Nunca se mantiene durante llamadas a OBS.
	mutable std::mutex mutex;
	QJsonObject config;
	// El hilo de gráficos los lee (get_width/get_height) mientras la interfaz los cambia.
	std::atomic<uint32_t> width{500};
	std::atomic<uint32_t> height{600};
	bool sized = false; // la de espectadores ya tomó el tamaño de su recuadro
};

// OBS puede crear y destruir fuentes desde otros hilos: el acceso a la lista va con candado.
// El candado solo cubre la lista (y lastSources): nunca se mantiene durante llamadas a OBS que
// toman los candados de escenas o fuentes, porque OBS llama a ov_destroy con esos candados
// tomados y se bloquearían mutuamente.
std::mutex sourcesMutex;
std::set<OverlaySource *> sources;
// Con OBS transmitiendo nunca se muestran los mensajes de ejemplo.
std::atomic<bool> streaming{false};

void send(OverlaySource *s, const QJsonObject &payload)
{
	if (!s->browser)
		return;
	const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_string(&cd, "eventName", "multichat");
	calldata_set_string(&cd, "jsonString", json.constData());
	proc_handler_call(obs_source_get_proc_handler(s->browser), "javascript_event", &cd);
	calldata_free(&cd);
}

// Últimos canales conocidos (live y espectadores), para las fuentes de espectadores.
// Va con sourcesMutex.
QJsonArray lastSources;

QJsonObject configOf(const OverlaySource *s)
{
	std::lock_guard<std::mutex> lock(s->mutex);
	return s->config;
}

// Recorre las fuentes sin mantener el candado: bajo el candado solo toma una referencia de cada
// una (las que ya se están destruyendo no la dan), y la suelta después de usarla.
template<typename Fn> void forEachSource(Fn &&fn)
{
	std::vector<std::pair<OverlaySource *, obs_source_t *>> refs;
	{
		std::lock_guard<std::mutex> lock(sourcesMutex);
		refs.reserve(sources.size());
		for (OverlaySource *s : sources)
			if (obs_source_t *ref = obs_source_get_ref(s->self))
				refs.emplace_back(s, ref);
	}
	for (auto &[s, ref] : refs) {
		fn(s);
		obs_source_release(ref);
	}
}

// ---- Fuente del mensaje destacado ----

// Último estado del destacado, para mandarlo a las fuentes nuevas y tras recargar la página.
// Se escribe en el hilo de la interfaz y se lee desde donde OBS cree las fuentes.
std::mutex featuredMutex;
QJsonObject featuredState{{"type", "featured"}, {"message", QJsonValue::Null}, {"at", 0}};

// La página cuenta el tiempo para ocultarlo desde "at" (cuando se destacó), con "now" para
// corregir la diferencia de reloj; así recargarla no vuelve a mostrar uno que ya se ocultó.
void sendFeatured(OverlaySource *s)
{
	QJsonObject payload;
	{
		std::lock_guard<std::mutex> lock(featuredMutex);
		payload = featuredState;
	}
	payload.insert("now", QDateTime::currentMSecsSinceEpoch());
	send(s, payload);
}

void sendAll(const QJsonObject &payload)
{
	forEachSource([&](OverlaySource *s) {
		if (s->kind == Kind::Chat)
			send(s, payload);
	});
}

// ---- Fuente de espectadores ----

struct ViewerItem {
	QString platform, name;
	int viewers = -1;
};

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
	if (items.isEmpty() && config.value("preview").toBool() && !streaming) {
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

void refresh(OverlaySource *s)
{
	if (s->kind == Kind::Viewers)
		refreshViewers(s);
	else if (s->kind == Kind::Clock)
		refreshClock(s);
}

void sendConfig(OverlaySource *s)
{
	QJsonObject config = configOf(s);
	config.insert("streaming", streaming.load());
	send(s, QJsonObject{{"type", "config"}, {"config", config}});
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

// Color de una propiedad de OBS (0xAABBGGRR) como "#rrggbb".
QString hexColor(obs_data_t *settings, const char *key)
{
	const auto c = uint32_t(obs_data_get_int(settings, key));
	return QStringLiteral("#%1%2%3")
		.arg(c & 0xff, 2, 16, QLatin1Char('0'))
		.arg((c >> 8) & 0xff, 2, 16, QLatin1Char('0'))
		.arg((c >> 16) & 0xff, 2, 16, QLatin1Char('0'));
}

// Estilo de un fondo (color, opacidad, redondeo y borde). prefix: "" para el fondo de los
// mensajes o del recuadro, "panel_" para el fondo general del chat.
void readBoxStyle(QJsonObject &config, obs_data_t *settings, const std::string &prefix, const QString &js)
{
	const std::string color = prefix.empty() ? "bg_color" : prefix + "color";
	config.insert(js + "Color", hexColor(settings, color.c_str()));
	config.insert(js + "Opacity", (int)obs_data_get_int(settings, (prefix + "opacity").c_str()));
	config.insert(js + "Radius", (int)obs_data_get_int(settings, (prefix + "radius").c_str()));
	config.insert(js + "BorderWidth", (int)obs_data_get_int(settings, (prefix + "border_width").c_str()));
	config.insert(js + "BorderColor", hexColor(settings, (prefix + "border_color").c_str()));
}

void addBoxStyle(obs_properties_t *props, const std::string &prefix)
{
	// Textos "Style.*" para el fondo de los mensajes o del recuadro, "Panel.*" para el general.
	const std::string group = prefix.empty() ? "Style." : "Panel.";
	auto text = [&](const char *name) { return obs_module_text((group + name).c_str()); };
	const std::string color = prefix.empty() ? "bg_color" : prefix + "color";
	obs_properties_add_color(props, color.c_str(), text("Color"));
	obs_property_t *opacity =
		obs_properties_add_int_slider(props, (prefix + "opacity").c_str(), text("Opacity"), 0, 100, 5);
	obs_property_int_set_suffix(opacity, " %");
	obs_property_t *radius =
		obs_properties_add_int_slider(props, (prefix + "radius").c_str(), text("Radius"), 0, 100, 1);
	obs_property_int_set_suffix(radius, " px");
	obs_property_t *border =
		obs_properties_add_int_slider(props, (prefix + "border_width").c_str(), text("Border"), 0, 20, 1);
	obs_property_int_set_suffix(border, " px");
	obs_properties_add_color(props, (prefix + "border_color").c_str(), text("BorderColor"));
}

// Relleno arriba/abajo e izquierda/derecha.
void addPadding(obs_properties_t *props, const char *keyY, const char *keyX, const char *textY, const char *textX)
{
	obs_property_t *y = obs_properties_add_int_slider(props, keyY, obs_module_text(textY), 0, 100, 1);
	obs_property_int_set_suffix(y, " px");
	obs_property_t *x = obs_properties_add_int_slider(props, keyX, obs_module_text(textX), 0, 200, 1);
	obs_property_int_set_suffix(x, " px");
}

// Estilos de fondo de data/overlay/styles.css (los mismos que music-now-playing).
void addStyleList(obs_properties_t *props)
{
	obs_property_t *style = obs_properties_add_list(props, "style", obs_module_text("Style"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const char *id : {"solid", "glass", "pill", "macos", "minimal", "line", "synthwave", "neoncity", "cyber",
			       "retro", "glitch"})
		obs_property_list_add_string(style, obs_module_text(("Style." + std::string(id)).c_str()), id);
	obs_property_set_long_description(style, obs_module_text("Style.Help"));
}

// Solo se ven los controles que afectan al estilo elegido: el color en Sólido y en los de
// cristal (es su tinte), el redondeo salvo en Píldora y Cyberpunk, y el borde en Sólido.
void showBoxStyle(obs_properties_t *props, obs_data_t *settings, const std::string &prefix, bool visible)
{
	const std::string style = obs_data_get_string(settings, "style");
	const bool tinted = style == "solid" || style == "glass" || style == "pill" || style == "macos";
	const bool rounded = style != "pill" && style != "cyber";
	const std::string color = prefix.empty() ? "bg_color" : prefix + "color";
	auto show = [&](const std::string &key, bool on) {
		obs_property_set_visible(obs_properties_get(props, key.c_str()), visible && on);
	};
	show(color, tinted);
	show(prefix + "opacity", true);
	show(prefix + "radius", rounded);
	show(prefix + "border_width", style == "solid");
	show(prefix + "border_color", style == "solid");
}

// ---- Settings table (defaults and presets live in overlay-presets.cpp) ----

void applyDefaults(obs_data_t *settings, Kind kind)
{
	for (const overlay::Setting &setting : overlay::defaultSettings(kind)) {
		const char *key = setting.key.c_str();
		if (const bool *b = std::get_if<bool>(&setting.value))
			obs_data_set_default_bool(settings, key, *b);
		else if (const long long *i = std::get_if<long long>(&setting.value))
			obs_data_set_default_int(settings, key, *i);
		else
			obs_data_set_default_string(settings, key, std::get<std::string>(setting.value).c_str());
	}
}

void applySetting(obs_data_t *settings, const overlay::Setting &setting)
{
	const char *key = setting.key.c_str();
	if (const bool *b = std::get_if<bool>(&setting.value))
		obs_data_set_bool(settings, key, *b);
	else if (const long long *i = std::get_if<long long>(&setting.value))
		obs_data_set_int(settings, key, *i);
	else
		obs_data_set_string(settings, key, std::get<std::string>(setting.value).c_str());
}

bool hasSetting(obs_data_t *settings, const overlay::Setting &setting)
{
	const char *key = setting.key.c_str();
	if (const bool *b = std::get_if<bool>(&setting.value))
		return obs_data_get_bool(settings, key) == *b;
	if (const long long *i = std::get_if<long long>(&setting.value))
		return obs_data_get_int(settings, key) == *i;
	return std::get<std::string>(setting.value) == obs_data_get_string(settings, key);
}

// ---- Typography (text group of every source) ----

// Chat and featured show names and wrap lines; viewers and clock are a single line of numbers.
bool hasNamesAndLines(Kind kind)
{
	return kind == Kind::Chat || kind == Kind::Featured;
}

QString cssColorOf(obs_data_t *settings, const char *key, bool withAlpha)
{
	return QString::fromStdString(overlay::cssColor(uint32_t(obs_data_get_int(settings, key)), withAlpha));
}

void readTextStyle(QJsonObject &config, obs_data_t *settings, Kind kind)
{
	const std::string face = obs_data_get_string(settings, "font_family");
	config.insert("fontFace", QString::fromStdString(face));
	config.insert("font", QString::fromStdString(overlay::cssFontFamily(face)));
	config.insert("textWeight", (int)obs_data_get_int(settings, "font_weight"));
	config.insert("textColor", cssColorOf(settings, "text_color", false));
	config.insert("outlineWidth", (int)obs_data_get_int(settings, "outline_width"));
	config.insert("outlineColor", cssColorOf(settings, "outline_color", false));
	config.insert("shadowMode", obs_data_get_string(settings, "text_shadow"));
	config.insert("shadowColor", cssColorOf(settings, "shadow_color", true));
	config.insert("shadowBlur", (int)obs_data_get_int(settings, "shadow_blur"));
	if (hasNamesAndLines(kind)) {
		config.insert("nameMode", obs_data_get_string(settings, "name_mode"));
		config.insert("nameColor", cssColorOf(settings, "name_color", false));
		config.insert("lineHeight", (int)obs_data_get_int(settings, "line_height"));
	}
}

void addTextStyle(obs_properties_t *props, Kind kind)
{
	obs_property_t *family = obs_properties_add_list(props, "font_family", obs_module_text("Text.Font"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(family, obs_module_text("Text.Font.Default"), "");
	for (const QString &name : QFontDatabase::families()) {
		if (name.startsWith('.') || QFontDatabase::isPrivateFamily(name))
			continue;
		const QByteArray utf8 = name.toUtf8();
		obs_property_list_add_string(family, utf8.constData(), utf8.constData());
	}
	obs_property_set_long_description(family, obs_module_text("Text.Font.Help"));

	obs_property_t *weight = obs_properties_add_list(props, "font_weight", obs_module_text("Text.Weight"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int w : {300, 400, 500, 600, 700, 800, 900})
		obs_property_list_add_int(weight, obs_module_text(("Text.Weight." + std::to_string(w)).c_str()), w);
	obs_property_set_long_description(
		weight, obs_module_text(hasNamesAndLines(kind) ? "Text.Weight.Help" : "Text.Weight.HelpBox"));

	obs_properties_add_color(props, "text_color", obs_module_text("Text.Color"));
	if (hasNamesAndLines(kind)) {
		obs_property_t *mode = obs_properties_add_list(props, "name_mode", obs_module_text("Text.NameMode"),
							       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		for (const char *id : {"user", "platform", "fixed"})
			obs_property_list_add_string(mode, obs_module_text(("Text.NameMode." + std::string(id)).c_str()), id);
		obs_properties_add_color(props, "name_color", obs_module_text("Text.NameColor"));
	}

	obs_property_t *outline =
		obs_properties_add_int_slider(props, "outline_width", obs_module_text("Text.Outline"), 0, 10, 1);
	obs_property_int_set_suffix(outline, " px");
	obs_properties_add_color(props, "outline_color", obs_module_text("Text.OutlineColor"));

	obs_property_t *shadow = obs_properties_add_list(props, "text_shadow", obs_module_text("Text.Shadow"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const char *id : {"auto", "on", "off"})
		obs_property_list_add_string(shadow, obs_module_text(("Text.Shadow." + std::string(id)).c_str()), id);
	obs_property_set_long_description(shadow, obs_module_text("Text.Shadow.Help"));
	obs_properties_add_color_alpha(props, "shadow_color", obs_module_text("Text.ShadowColor"));
	obs_property_t *blur =
		obs_properties_add_int_slider(props, "shadow_blur", obs_module_text("Text.ShadowBlur"), 0, 30, 1);
	obs_property_int_set_suffix(blur, " px");

	if (hasNamesAndLines(kind)) {
		obs_property_t *lines =
			obs_properties_add_int_slider(props, "line_height", obs_module_text("Text.LineHeight"), 80, 250, 5);
		obs_property_int_set_suffix(lines, " %");
	}
}

// Controls that only matter with another option: the fixed name color, the outline color and
// the shadow color and blur.
void showTextStyle(obs_properties_t *props, obs_data_t *settings)
{
	obs_property_set_visible(obs_properties_get(props, "name_color"),
				 strcmp(obs_data_get_string(settings, "name_mode"), "fixed") == 0);
	obs_property_set_visible(obs_properties_get(props, "outline_color"),
				 obs_data_get_int(settings, "outline_width") > 0);
	const bool shadow = strcmp(obs_data_get_string(settings, "text_shadow"), "off") != 0;
	obs_property_set_visible(obs_properties_get(props, "shadow_color"), shadow);
	obs_property_set_visible(obs_properties_get(props, "shadow_blur"), shadow);
}

// Los controles del fondo solo se ven con su casilla activada.
bool box_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	showBoxStyle(props, settings, "", obs_data_get_bool(settings, "background"));
	obs_property_set_visible(obs_properties_get(props, "style"), obs_data_get_bool(settings, "background"));
	showTextStyle(props, settings);
	return true;
}

QString moduleString(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// Texts the pages show on their own (badge label and sample messages), in OBS's language.
void addPageTexts(QJsonObject &config, Kind kind)
{
	config.insert("memberBadge", moduleString("Badge.Member"));
	if (kind == Kind::Featured) {
		config.insert("sampleText", moduleString("Featured.SampleText"));
		return;
	}
	QJsonArray samples;
	for (int i = 1; i <= 7; ++i)
		samples.append(moduleString(("Overlay.Sample" + std::to_string(i)).c_str()));
	config.insert("samples", samples);
}

QJsonObject readConfig(Kind kind, obs_data_t *settings)
{
	QJsonObject config;
	if (kind == Kind::Featured) {
		config = QJsonObject{
			{"size", (int)obs_data_get_int(settings, "font_size")},
			{"animation", obs_data_get_string(settings, "animation")},
			{"autoHide", obs_data_get_bool(settings, "auto_hide")},
			{"hideBase", (int)obs_data_get_int(settings, "hide_base")},
			{"hidePerChar", (int)obs_data_get_int(settings, "hide_per_char")},
			{"hideMax", (int)obs_data_get_int(settings, "hide_max")},
			{"valign", obs_data_get_string(settings, "valign")},
			{"halign", obs_data_get_string(settings, "halign")},
			{"icons", obs_data_get_bool(settings, "icons")},
			{"badges", obs_data_get_bool(settings, "badges")},
			{"padY", (int)obs_data_get_int(settings, "pad_y")},
			{"padX", (int)obs_data_get_int(settings, "pad_x")},
			{"background", obs_data_get_bool(settings, "background")},
			{"style", obs_data_get_string(settings, "style")},
			{"preview", obs_data_get_bool(settings, "preview")},
		};
		readBoxStyle(config, settings, "", "bg");
		readTextStyle(config, settings, kind);
		addPageTexts(config, kind);
		return config;
	}
	QJsonArray platforms;
	for (const char *p : {"twitch", "youtube", "kick", "tiktok", "starvios"})
		if (obs_data_get_bool(settings, (std::string("show_") + p).c_str()))
			platforms.append(p);
	if (kind == Kind::Clock) {
		config = QJsonObject{
			{"size", (int)obs_data_get_int(settings, "font_size")},
			{"date", obs_data_get_bool(settings, "date")},
			{"time", obs_data_get_bool(settings, "time")},
			{"seconds", obs_data_get_bool(settings, "seconds")},
			{"hour24", obs_data_get_bool(settings, "hour24")},
			{"bar", obs_data_get_bool(settings, "bar")},
			{"padY", (int)obs_data_get_int(settings, "pad_y")},
			{"padX", (int)obs_data_get_int(settings, "pad_x")},
			{"background", obs_data_get_bool(settings, "background")},
			{"style", obs_data_get_string(settings, "style")},
		};
		readBoxStyle(config, settings, "", "bg");
		readTextStyle(config, settings, kind);
		return config;
	}
	if (kind == Kind::Viewers) {
		config = QJsonObject{
			{"size", (int)obs_data_get_int(settings, "font_size")},
			{"layout", obs_data_get_string(settings, "layout")},
			{"total", obs_data_get_bool(settings, "total")},
			{"names", obs_data_get_bool(settings, "names")},
			{"padY", (int)obs_data_get_int(settings, "pad_y")},
			{"padX", (int)obs_data_get_int(settings, "pad_x")},
			{"platforms", platforms},
			{"background", obs_data_get_bool(settings, "background")},
			{"style", obs_data_get_string(settings, "style")},
			{"preview", obs_data_get_bool(settings, "preview")},
		};
		readBoxStyle(config, settings, "", "bg");
		readTextStyle(config, settings, kind);
		return config;
	}
	config = QJsonObject{
		{"size", (int)obs_data_get_int(settings, "font_size")},
		{"fade", (int)obs_data_get_int(settings, "fade")},
		{"max", (int)obs_data_get_int(settings, "max")},
		{"platforms", platforms},
		{"icons", obs_data_get_bool(settings, "icons")},
		{"badges", obs_data_get_bool(settings, "badges")},
		{"background", obs_data_get_bool(settings, "background")},
		{"panel", obs_data_get_bool(settings, "panel")},
		{"style", obs_data_get_string(settings, "style")},
		{"align", obs_data_get_string(settings, "align")},
		{"layout", obs_data_get_string(settings, "layout")},
		{"speed", (int)obs_data_get_int(settings, "speed")},
		{"spacing", (int)obs_data_get_int(settings, "spacing")},
		{"hspacing", (int)obs_data_get_int(settings, "hspacing")},
		{"padY", (int)obs_data_get_int(settings, "pad_y")},
		{"padX", (int)obs_data_get_int(settings, "pad_x")},
		{"msgPadY", (int)obs_data_get_int(settings, "msg_pad_y")},
		{"msgPadX", (int)obs_data_get_int(settings, "msg_pad_x")},
		{"scroll", obs_data_get_string(settings, "scroll")},
		{"clock", obs_data_get_bool(settings, "clock")},
		{"clockDate", obs_data_get_bool(settings, "clock_date")},
		{"clockTime", obs_data_get_bool(settings, "clock_time")},
		{"clockSeconds", obs_data_get_bool(settings, "clock_seconds")},
		{"clockHour24", obs_data_get_bool(settings, "clock_hour24")},
		{"clockBar", obs_data_get_bool(settings, "clock_bar")},
		{"fit", obs_data_get_bool(settings, "fit_canvas")},
		{"tickerWidth", (int)obs_data_get_int(settings, "ticker_width")},
		{"preview", obs_data_get_bool(settings, "preview")},
	};
	readBoxStyle(config, settings, "", "bg");
	readBoxStyle(config, settings, "panel_", "panel");
	readTextStyle(config, settings, kind);
	addPageTexts(config, kind);
	return config;
}

void readSettings(OverlaySource *s, obs_data_t *settings)
{
	QJsonObject config = readConfig(s->kind, settings);
	config.insert("customCss",
		      QString::fromStdString(overlay::sanitizeCustomCss(obs_data_get_string(settings, "custom_css"))));
	if (s->kind == Kind::Chat || s->kind == Kind::Featured) { // espectadores y reloj calculan su tamaño solos
		s->width = (uint32_t)std::max<long long>(30, obs_data_get_int(settings, "width"));
		s->height = (uint32_t)std::max<long long>(30, obs_data_get_int(settings, "height"));
	}
	if (fitsCanvas(s->kind, config)) {
		s->width = tickerWidth(config);
		s->height = tickerHeight(config);
	}
	std::lock_guard<std::mutex> lock(s->mutex);
	s->config = config;
}

const char *ov_get_name(void *)
{
	return obs_module_text("Source.Name");
}

const char *vw_get_name(void *)
{
	return obs_module_text("Viewers.Name");
}

void *create(obs_data_t *settings, obs_source_t *source, Kind kind)
{
	auto *s = new OverlaySource();
	s->kind = kind;
	s->self = source;
	readSettings(s, settings);
	obs_data_t *bs = browserSettings(s);
	s->browser = obs_source_create_private("browser_source", "multichat-overlay", bs);
	obs_data_release(bs);
	if (!s->browser)
		obs_log(LOG_WARNING, "could not create the browser source (is obs-browser missing?)");
	{
		std::lock_guard<std::mutex> lock(sourcesMutex);
		sources.insert(s);
	}
	sendConfig(s);
	refresh(s);
	if (kind == Kind::Featured)
		sendFeatured(s);
	return s;
}

void *ov_create(obs_data_t *settings, obs_source_t *source)
{
	return create(settings, source, Kind::Chat);
}

void *vw_create(obs_data_t *settings, obs_source_t *source)
{
	return create(settings, source, Kind::Viewers);
}

void *ck_create(obs_data_t *settings, obs_source_t *source)
{
	return create(settings, source, Kind::Clock);
}

void *fe_create(obs_data_t *settings, obs_source_t *source)
{
	return create(settings, source, Kind::Featured);
}

void ov_destroy(void *data)
{
	auto *s = static_cast<OverlaySource *>(data);
	{
		std::lock_guard<std::mutex> lock(sourcesMutex);
		sources.erase(s);
	}
	obs_source_release(s->browser);
	delete s;
}

void ov_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<OverlaySource *>(data);
	// Sin candados: resizeBrowser y fitAllScenes toman los de la fuente de navegador y de
	// las escenas.
	const bool wasFitting = fitsCanvas(s->kind, configOf(s));
	readSettings(s, settings);
	resizeBrowser(s);
	if (!wasFitting && fitsCanvas(s->kind, configOf(s)))
		fitAllScenes(s);
	sendConfig(s);
	refresh(s);
}

void ov_defaults(obs_data_t *settings)
{
	applyDefaults(settings, Kind::Chat);
}

void vw_defaults(obs_data_t *settings)
{
	applyDefaults(settings, Kind::Viewers);
}

// En horizontal no se usan "apilar", "ocultar después de", el máximo ni el tamaño (es automático);
// la velocidad solo en horizontal.
bool ov_layout_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool horizontal = strcmp(obs_data_get_string(settings, "layout"), "horizontal") == 0;
	// Versiones de prueba anteriores guardaban aquí el tamaño vertical al pasar a horizontal.
	if (!horizontal && obs_data_has_user_value(settings, "vertical_width")) {
		obs_data_set_int(settings, "width", obs_data_get_int(settings, "vertical_width"));
		obs_data_set_int(settings, "height", obs_data_get_int(settings, "vertical_height"));
		for (const char *key : {"vertical_width", "vertical_height", "fitted"})
			obs_data_erase(settings, key);
	}
	const bool manualSize = !horizontal || !obs_data_get_bool(settings, "fit_canvas");
	obs_property_set_visible(obs_properties_get(props, "width"), manualSize);
	obs_property_set_visible(obs_properties_get(props, "height"), manualSize);
	obs_property_set_visible(obs_properties_get(props, "fit_canvas"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "ticker_width"), horizontal && !manualSize);
	obs_property_set_visible(obs_properties_get(props, "scroll"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "group_clock"), horizontal);
	const bool clockOn = horizontal && obs_data_get_bool(settings, "clock");
	for (const char *key : {"clock_date", "clock_time", "clock_seconds", "clock_hour24", "clock_bar"})
		obs_property_set_visible(obs_properties_get(props, key), clockOn);
	obs_property_set_visible(obs_properties_get(props, "speed"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "hspacing"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "spacing"), !horizontal);
	obs_property_set_visible(obs_properties_get(props, "align"), !horizontal);
	// Hiding and the message limit only apply to the vertical list: the whole section goes.
	obs_property_set_visible(obs_properties_get(props, "group_animation"), !horizontal);
	showBoxStyle(props, settings, "", obs_data_get_bool(settings, "background"));
	showBoxStyle(props, settings, "panel_", obs_data_get_bool(settings, "panel"));
	obs_property_set_visible(obs_properties_get(props, "style"),
				 obs_data_get_bool(settings, "background") || obs_data_get_bool(settings, "panel"));
	for (const char *key : {"msg_pad_y", "msg_pad_x"})
		obs_property_set_visible(obs_properties_get(props, key), obs_data_get_bool(settings, "background"));
	showTextStyle(props, settings);
	return true;
}

bool fe_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);

// ---- Presets, custom CSS and the shared modified callback ----

void *kindParam(Kind kind)
{
	return reinterpret_cast<void *>(static_cast<intptr_t>(kind));
}

void addPreset(obs_properties_t *props)
{
	obs_property_t *preset = obs_properties_add_list(props, "preset", obs_module_text("Preset"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(preset, obs_module_text("Preset.custom"), overlay::kCustomPreset);
	for (const std::string &id : overlay::presetIds())
		obs_property_list_add_string(preset, obs_module_text(("Preset." + id).c_str()), id.c_str());
	obs_property_set_long_description(preset, obs_module_text("Preset.Help"));
}

void addCustomCss(obs_properties_t *props)
{
	obs_property_t *css =
		obs_properties_add_text(props, "custom_css", obs_module_text("Advanced.CustomCss"), OBS_TEXT_MULTILINE);
	obs_property_set_long_description(css, obs_module_text("Advanced.CustomCss.Help"));
}

// Properties whose value shows or hides others: changing them refreshes the window. Any other
// change only refreshes when it takes the source off its preset, so typing in a text box or
// dragging a slider is not interrupted.
bool changesVisibility(const char *name)
{
	for (const char *key : {"layout", "fit_canvas", "clock", "style", "background", "panel", "auto_hide",
				"name_mode", "outline_width", "text_shadow"})
		if (strcmp(name, key) == 0)
			return true;
	return false;
}

bool on_modified(void *param, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	const auto kind = static_cast<Kind>(reinterpret_cast<intptr_t>(param));
	auto updateVisibility = [&] {
		if (kind == Kind::Chat)
			ov_layout_changed(props, property, settings);
		else if (kind == Kind::Featured)
			fe_changed(props, property, settings);
		else
			box_changed(props, property, settings);
	};
	const char *name = obs_property_name(property);
	const std::string preset = obs_data_get_string(settings, "preset");
	if (strcmp(name, "preset") == 0) {
		// OBS calls this again every time the window opens or refreshes: the preset is written
		// only when a different one is chosen, so it never undoes later changes.
		if (preset == obs_data_get_string(settings, "preset_applied"))
			return false;
		for (const overlay::Setting &setting : overlay::presetSettings(kind, preset))
			applySetting(settings, setting);
		obs_data_set_string(settings, "preset_applied", preset.c_str());
		updateVisibility();
		return true;
	}
	updateVisibility();
	bool refresh = changesVisibility(name);
	// Back to "Custom" as soon as a value the preset wrote is changed.
	if (preset != overlay::kCustomPreset) {
		for (const overlay::Setting &setting : overlay::presetSettings(kind, preset)) {
			if (hasSetting(settings, setting))
				continue;
			obs_data_set_string(settings, "preset", overlay::kCustomPreset);
			obs_data_set_string(settings, "preset_applied", overlay::kCustomPreset);
			refresh = true;
			break;
		}
	}
	return refresh;
}

// Every property goes through on_modified (visibility and the preset).
void hookAll(obs_properties_t *props, Kind kind)
{
	for (obs_property_t *p = obs_properties_first(props); p; obs_property_next(&p)) {
		const obs_property_type type = obs_property_get_type(p);
		if (type == OBS_PROPERTY_GROUP) {
			hookAll(obs_property_group_content(p), kind);
			continue;
		}
		if (type == OBS_PROPERTY_BUTTON)
			continue;
		obs_property_set_modified_callback2(p, on_modified, kindParam(kind));
	}
}

bool ov_open_settings(obs_properties_t *, obs_property_t *, void *)
{
	open_multichat_settings_from_source();
	return false;
}

// Sections of the properties window. The settings inside a group stay flat in the same
// obs_data, so the keys saved by older versions keep working.
obs_properties_t *addGroup(obs_properties_t *props, const char *name, const char *text)
{
	obs_properties_t *group = obs_properties_create();
	obs_properties_add_group(props, name, obs_module_text(text), OBS_GROUP_NORMAL, group);
	return group;
}

void addPlatforms(obs_properties_t *props)
{
	obs_properties_add_bool(props, "show_twitch", obs_module_text("Source.ShowTwitch"));
	obs_properties_add_bool(props, "show_youtube", obs_module_text("Source.ShowYouTube"));
	obs_properties_add_bool(props, "show_kick", obs_module_text("Source.ShowKick"));
	obs_properties_add_bool(props, "show_tiktok", obs_module_text("Source.ShowTikTok"));
	obs_properties_add_bool(props, "show_starvios", obs_module_text("Source.ShowStarvios"));
}

obs_properties_t *ov_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_button2(props, "chats", obs_module_text("Source.Chats"), ov_open_settings, nullptr);
	addPreset(props);

	obs_properties_t *design = addGroup(props, "group_layout", "Group.Layout");
	obs_property_t *layout = obs_properties_add_list(design, "layout", obs_module_text("Source.Layout"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(layout, obs_module_text("Source.Layout.Vertical"), "vertical");
	obs_property_list_add_string(layout, obs_module_text("Source.Layout.Horizontal"), "horizontal");
	obs_property_set_long_description(layout, obs_module_text("Source.Layout.Help"));
	obs_property_t *fit = obs_properties_add_bool(design, "fit_canvas", obs_module_text("Source.FitCanvas"));
	obs_property_set_long_description(fit, obs_module_text("Source.FitCanvas.Help"));
	obs_property_t *tickerWidthProp =
		obs_properties_add_int(design, "ticker_width", obs_module_text("Source.TickerWidth"), 0, 8192, 1);
	obs_property_int_set_suffix(tickerWidthProp, " px");
	obs_property_set_long_description(tickerWidthProp, obs_module_text("Source.TickerWidth.Help"));
	obs_properties_add_int(design, "width", obs_module_text("Source.Width"), 100, 4096, 1);
	obs_properties_add_int(design, "height", obs_module_text("Source.Height"), 100, 4096, 1);
	obs_property_t *align = obs_properties_add_list(design, "align", obs_module_text("Source.Align"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(align, obs_module_text("Source.Align.Bottom"), "bottom");
	obs_property_list_add_string(align, obs_module_text("Source.Align.Top"), "top");
	obs_property_t *scroll = obs_properties_add_list(design, "scroll", obs_module_text("Source.Scroll"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(scroll, obs_module_text("Source.Scroll.Continuous"), "continuous");
	obs_property_list_add_string(scroll, obs_module_text("Source.Scroll.Message"), "message");
	obs_property_t *speed =
		obs_properties_add_int_slider(design, "speed", obs_module_text("Source.Speed"), 20, 600, 10);
	obs_property_int_set_suffix(speed, " px/s");
	obs_property_t *spacing =
		obs_properties_add_int_slider(design, "spacing", obs_module_text("Source.Spacing"), -40, 60, 1);
	obs_property_int_set_suffix(spacing, " px");
	obs_property_t *hspacing =
		obs_properties_add_int_slider(design, "hspacing", obs_module_text("Source.Spacing"), -200, 400, 2);
	obs_property_int_set_suffix(hspacing, " px");
	addPadding(design, "pad_y", "pad_x", "Source.PadY", "Source.PadX");

	obs_properties_t *text = addGroup(props, "group_text", "Group.Text");
	obs_property_t *size =
		obs_properties_add_int_slider(text, "font_size", obs_module_text("Source.FontSize"), 10, 72, 1);
	obs_property_int_set_suffix(size, " px");
	addTextStyle(text, Kind::Chat);

	obs_properties_t *platforms = addGroup(props, "group_platforms", "Group.Platforms");
	addPlatforms(platforms);
	obs_properties_add_bool(platforms, "icons", obs_module_text("Source.Icons"));
	obs_properties_add_bool(platforms, "badges", obs_module_text("Source.Badges"));

	obs_properties_t *background = addGroup(props, "group_background", "Group.Background");
	obs_properties_add_bool(background, "background", obs_module_text("Source.Background"));
	addStyleList(background);
	addBoxStyle(background, "");
	addPadding(background, "msg_pad_y", "msg_pad_x", "Source.MsgPadY", "Source.MsgPadX");
	obs_properties_add_bool(background, "panel", obs_module_text("Source.Panel"));
	addBoxStyle(background, "panel_");

	obs_properties_t *animation = addGroup(props, "group_animation", "Group.Animation");
	obs_property_t *fade = obs_properties_add_int(animation, "fade", obs_module_text("Source.Fade"), 0, 600, 1);
	obs_property_int_set_suffix(fade, " s");
	obs_property_set_long_description(fade, obs_module_text("Source.Fade.Help"));
	obs_properties_add_int(animation, "max", obs_module_text("Source.Max"), 1, 100, 1);

	obs_properties_t *clock = addGroup(props, "group_clock", "Group.TickerClock");
	obs_property_t *clockOn = obs_properties_add_bool(clock, "clock", obs_module_text("Source.Clock"));
	obs_property_set_long_description(clockOn, obs_module_text("Source.Clock.Help"));
	obs_properties_add_bool(clock, "clock_date", obs_module_text("Clock.Date"));
	obs_properties_add_bool(clock, "clock_time", obs_module_text("Clock.Time"));
	obs_properties_add_bool(clock, "clock_seconds", obs_module_text("Clock.Seconds"));
	obs_properties_add_bool(clock, "clock_hour24", obs_module_text("Clock.Hour24"));
	obs_properties_add_bool(clock, "clock_bar", obs_module_text("Clock.Bar"));

	obs_properties_t *advanced = addGroup(props, "group_advanced", "Group.Advanced");
	obs_properties_add_bool(advanced, "preview", obs_module_text("Source.Preview"));
	addCustomCss(advanced);
	hookAll(props, Kind::Chat);
	return props;
}

obs_properties_t *vw_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_button2(props, "chats", obs_module_text("Source.Chats"), ov_open_settings, nullptr);
	addPreset(props);

	obs_properties_t *design = addGroup(props, "group_layout", "Group.Layout");
	obs_property_t *layout = obs_properties_add_list(design, "layout", obs_module_text("Viewers.Layout"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(layout, obs_module_text("Viewers.Layout.Row"), "row");
	obs_property_list_add_string(layout, obs_module_text("Viewers.Layout.Column"), "column");
	obs_properties_add_bool(design, "total", obs_module_text("Viewers.Total"));
	obs_properties_add_bool(design, "names", obs_module_text("Viewers.Names"));
	addPadding(design, "pad_y", "pad_x", "Box.PadY", "Box.PadX");

	obs_properties_t *text = addGroup(props, "group_text", "Group.Text");
	obs_property_t *size =
		obs_properties_add_int_slider(text, "font_size", obs_module_text("Source.FontSize"), 10, 120, 1);
	obs_property_int_set_suffix(size, " px");
	addTextStyle(text, Kind::Viewers);

	addPlatforms(addGroup(props, "group_platforms", "Group.PlatformsOnly"));

	obs_properties_t *background = addGroup(props, "group_background", "Group.Background");
	obs_properties_add_bool(background, "background", obs_module_text("Viewers.Background"));
	addStyleList(background);
	addBoxStyle(background, "");

	obs_properties_t *advanced = addGroup(props, "group_advanced", "Group.Advanced");
	obs_properties_add_bool(advanced, "preview", obs_module_text("Viewers.Preview"));
	addCustomCss(advanced);
	hookAll(props, Kind::Viewers);
	return props;
}

const char *ck_get_name(void *)
{
	return obs_module_text("Clock.Name");
}

void ck_defaults(obs_data_t *settings)
{
	applyDefaults(settings, Kind::Clock);
}

obs_properties_t *ck_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	addPreset(props);
	obs_properties_t *clock = addGroup(props, "group_clock", "Group.Clock");
	obs_properties_add_bool(clock, "date", obs_module_text("Clock.Date"));
	obs_properties_add_bool(clock, "time", obs_module_text("Clock.Time"));
	obs_properties_add_bool(clock, "seconds", obs_module_text("Clock.Seconds"));
	obs_properties_add_bool(clock, "hour24", obs_module_text("Clock.Hour24"));
	obs_properties_add_bool(clock, "bar", obs_module_text("Clock.Bar"));

	obs_properties_t *text = addGroup(props, "group_text", "Group.Text");
	obs_property_t *size =
		obs_properties_add_int_slider(text, "font_size", obs_module_text("Source.FontSize"), 10, 160, 1);
	obs_property_int_set_suffix(size, " px");
	addTextStyle(text, Kind::Clock);

	obs_properties_t *design = addGroup(props, "group_layout", "Group.Layout");
	addPadding(design, "pad_y", "pad_x", "Box.PadY", "Box.PadX");

	obs_properties_t *background = addGroup(props, "group_background", "Group.Background");
	obs_properties_add_bool(background, "background", obs_module_text("Viewers.Background"));
	addStyleList(background);
	addBoxStyle(background, "");

	addCustomCss(addGroup(props, "group_advanced", "Group.Advanced"));
	hookAll(props, Kind::Clock);
	return props;
}

const char *fe_get_name(void *)
{
	return obs_module_text("Featured.Name");
}

void fe_defaults(obs_data_t *settings)
{
	applyDefaults(settings, Kind::Featured);
}

// Los tiempos de "ocultar solo" se ven con la casilla activada; el fondo, con la suya.
bool fe_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	const bool autoHide = obs_data_get_bool(settings, "auto_hide");
	for (const char *key : {"hide_base", "hide_per_char", "hide_max"})
		obs_property_set_visible(obs_properties_get(props, key), autoHide);
	return box_changed(props, property, settings);
}

obs_properties_t *fe_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	addPreset(props);
	obs_properties_t *design = addGroup(props, "group_layout", "Group.Layout");
	obs_property_t *valign = obs_properties_add_list(design, "valign", obs_module_text("Featured.VAlign"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const char *id : {"top", "center", "bottom"})
		obs_property_list_add_string(valign, obs_module_text(("Featured.VAlign." + std::string(id)).c_str()), id);
	obs_property_t *halign = obs_properties_add_list(design, "halign", obs_module_text("Featured.HAlign"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const char *id : {"left", "center", "right"})
		obs_property_list_add_string(halign, obs_module_text(("Featured.HAlign." + std::string(id)).c_str()), id);
	addPadding(design, "pad_y", "pad_x", "Box.PadY", "Box.PadX");
	obs_properties_add_int(design, "width", obs_module_text("Source.Width"), 100, 4096, 1);
	obs_properties_add_int(design, "height", obs_module_text("Source.Height"), 100, 4096, 1);

	obs_properties_t *text = addGroup(props, "group_text", "Group.Text");
	obs_property_t *size =
		obs_properties_add_int_slider(text, "font_size", obs_module_text("Source.FontSize"), 12, 120, 1);
	obs_property_int_set_suffix(size, " px");
	addTextStyle(text, Kind::Featured);

	obs_properties_t *platforms = addGroup(props, "group_platforms", "Group.Platforms");
	obs_properties_add_bool(platforms, "icons", obs_module_text("Source.Icons"));
	obs_properties_add_bool(platforms, "badges", obs_module_text("Source.Badges"));

	obs_properties_t *background = addGroup(props, "group_background", "Group.Background");
	obs_properties_add_bool(background, "background", obs_module_text("Viewers.Background"));
	addStyleList(background);
	addBoxStyle(background, "");

	obs_properties_t *animation = addGroup(props, "group_animation", "Group.Animation");
	obs_property_t *anim = obs_properties_add_list(animation, "animation", obs_module_text("Featured.Animation"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const char *id : {"slide", "fade", "zoom", "none"})
		obs_property_list_add_string(anim, obs_module_text(("Featured.Animation." + std::string(id)).c_str()), id);
	obs_property_t *autoHide =
		obs_properties_add_bool(animation, "auto_hide", obs_module_text("Featured.AutoHide"));
	obs_property_set_long_description(autoHide, obs_module_text("Featured.AutoHide.Help"));
	obs_property_t *base =
		obs_properties_add_int(animation, "hide_base", obs_module_text("Featured.HideBase"), 1, 300, 1);
	obs_property_int_set_suffix(base, " s");
	obs_property_t *perChar =
		obs_properties_add_int(animation, "hide_per_char", obs_module_text("Featured.HidePerChar"), 0, 1000, 5);
	obs_property_int_set_suffix(perChar, " ms");
	obs_property_t *max = obs_properties_add_int(animation, "hide_max", obs_module_text("Featured.HideMax"), 1, 600, 1);
	obs_property_int_set_suffix(max, " s");

	obs_properties_t *advanced = addGroup(props, "group_advanced", "Group.Advanced");
	obs_properties_add_bool(advanced, "preview", obs_module_text("Featured.Preview"));
	addCustomCss(advanced);
	hookAll(props, Kind::Featured);
	return props;
}

uint32_t ov_width(void *data)
{
	return static_cast<OverlaySource *>(data)->width;
}

uint32_t ov_height(void *data)
{
	return static_cast<OverlaySource *>(data)->height;
}

void ov_render(void *data, gs_effect_t *)
{
	auto *s = static_cast<OverlaySource *>(data);
	if (s->browser)
		obs_source_video_render(s->browser);
}

// La fuente de navegador se pausa si nadie la muestra: le pasamos nuestra visibilidad.
void ov_show(void *data)
{
	auto *s = static_cast<OverlaySource *>(data);
	if (s->browser)
		obs_source_inc_showing(s->browser);
}

void ov_hide(void *data)
{
	auto *s = static_cast<OverlaySource *>(data);
	if (s->browser)
		obs_source_dec_showing(s->browser);
}

void ov_activate(void *data)
{
	auto *s = static_cast<OverlaySource *>(data);
	if (s->browser)
		obs_source_inc_active(s->browser);
}

void ov_deactivate(void *data)
{
	auto *s = static_cast<OverlaySource *>(data);
	if (s->browser)
		obs_source_dec_active(s->browser);
}

void ov_enum_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	auto *s = static_cast<OverlaySource *>(data);
	if (s->browser)
		enum_callback(s->self, s->browser, param);
}

} // namespace

void overlay_set_streaming(bool active)
{
	streaming = active;
	forEachSource([](OverlaySource *s) {
		sendConfig(s);
		refresh(s); // al transmitir se quitan los números de ejemplo
	});
}

void register_overlay_source()
{
	ChatManager &manager = ChatManager::instance();
	QObject::connect(&manager, &ChatManager::message, &manager,
			 [](const QJsonObject &m) { sendAll(QJsonObject{{"type", "message"}, {"message", m}}); });
	QObject::connect(&manager, &ChatManager::cleared, &manager, [] { sendAll(QJsonObject{{"type", "clear"}}); });
	// Undo after clearing: the chat sources get the history back with their own event, not
	// through "message", so the voice source (which listens to ChatManager::message) does
	// not read them again. The page keeps at most its own limit anyway.
	QObject::connect(&manager, &ChatManager::restored, &manager, [] {
		const QJsonArray history = ChatManager::instance().history();
		QJsonArray recent;
		for (qsizetype i = std::max<qsizetype>(0, history.size() - 50); i < history.size(); ++i)
			recent.append(history.at(i));
		sendAll(QJsonObject{{"type", "restore"}, {"messages", recent}});
	});
	// The deletion event already has type "delete", source and the ids to remove.
	QObject::connect(&manager, &ChatManager::deleted, &manager, [](const QJsonObject &d) { sendAll(d); });
	auto pushViewers = [] {
		const QJsonArray list = ChatManager::instance().state().value("sources").toArray();
		{
			std::lock_guard<std::mutex> lock(sourcesMutex);
			lastSources = list;
		}
		forEachSource([](OverlaySource *s) {
			if (s->kind == Kind::Viewers)
				refreshViewers(s);
		});
	};
	QObject::connect(&manager, &ChatManager::stateChanged, &manager, pushViewers);
	QObject::connect(&manager, &ChatManager::featuredChanged, &manager, [](const QJsonObject &m) {
		{
			std::lock_guard<std::mutex> lock(featuredMutex);
			featuredState.insert("message", m.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m));
			featuredState.insert("at", ChatManager::instance().featuredAt());
		}
		forEachSource([](OverlaySource *s) {
			if (s->kind == Kind::Featured)
				sendFeatured(s);
		});
	});

	// La página puede recargarse (al cambiar el tamaño, al volver a la escena...):
	// le reenviamos la configuración cada pocos segundos.
	auto *heartbeat = new QTimer(&manager);
	heartbeat->setInterval(3000);
	QObject::connect(heartbeat, &QTimer::timeout, &manager, [] {
		forEachSource([](OverlaySource *s) {
			sendConfig(s);
			if (s->kind == Kind::Featured)
				sendFeatured(s);
		});
	});
	QObject::connect(heartbeat, &QTimer::timeout, &manager, pushViewers);
	// La cinta horizontal sigue al lienzo si cambia su resolución.
	QObject::connect(heartbeat, &QTimer::timeout, &manager, [] {
		forEachSource([](OverlaySource *s) {
			const QJsonObject config = configOf(s);
			if (fitsCanvas(s->kind, config) && s->width != tickerWidth(config)) {
				s->width = tickerWidth(config);
				resizeBrowser(s);
			}
		});
	});
	heartbeat->start();

	obs_source_info info = {};
	info.id = "multichat_overlay";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	info.get_name = ov_get_name;
	info.create = ov_create;
	info.destroy = ov_destroy;
	info.update = ov_update;
	info.get_defaults = ov_defaults;
	info.get_properties = ov_properties;
	info.get_width = ov_width;
	info.get_height = ov_height;
	info.video_render = ov_render;
	info.show = ov_show;
	info.hide = ov_hide;
	info.activate = ov_activate;
	info.deactivate = ov_deactivate;
	info.enum_active_sources = ov_enum_sources;
	info.enum_all_sources = ov_enum_sources;
	obs_register_source(&info);

	obs_source_info viewers = info;
	viewers.id = "multichat_viewers";
	viewers.get_name = vw_get_name;
	viewers.create = vw_create;
	viewers.get_defaults = vw_defaults;
	viewers.get_properties = vw_properties;
	obs_register_source(&viewers);

	obs_source_info clock = info;
	clock.id = "multichat_clock";
	clock.get_name = ck_get_name;
	clock.create = ck_create;
	clock.get_defaults = ck_defaults;
	clock.get_properties = ck_properties;
	obs_register_source(&clock);

	obs_source_info featured = info;
	featured.id = "multichat_featured";
	featured.get_name = fe_get_name;
	featured.create = fe_create;
	featured.get_defaults = fe_defaults;
	featured.get_properties = fe_properties;
	obs_register_source(&featured);
}
