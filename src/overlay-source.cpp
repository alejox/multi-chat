// Fuentes "Multichat" (el chat en la escena) y "Multichat (espectadores)". Por dentro son
// fuentes de navegador privadas con data/overlay/index.html y viewers.html; los mensajes y
// los espectadores les llegan con javascript_event.

#include "overlay-source.hpp"
#include "chat-manager.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
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
#include <cstring>
#include <atomic>
#include <mutex>
#include <set>
#include <string>

namespace {

enum class Kind { Chat, Viewers, Clock };

struct OverlaySource {
	Kind kind = Kind::Chat;
	obs_source_t *self = nullptr;
	obs_source_t *browser = nullptr;
	QJsonObject config;
	// El hilo de gráficos los lee (get_width/get_height) mientras la interfaz los cambia.
	std::atomic<uint32_t> width{500};
	std::atomic<uint32_t> height{600};
	bool sized = false; // la de espectadores ya tomó el tamaño de su recuadro
};

// OBS puede crear y destruir fuentes desde otros hilos: el acceso a la lista va con candado.
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
QJsonArray lastSources;

void sendAll(const QJsonObject &payload)
{
	std::lock_guard<std::mutex> lock(sourcesMutex);
	for (OverlaySource *s : sources)
		if (s->kind == Kind::Chat)
			send(s, payload);
}

// ---- Fuente de espectadores ----

struct ViewerItem {
	QString platform, name;
	int viewers = -1;
};

// Canales que muestra una fuente de espectadores: los que están en vivo o, para colocarla
// sin estar en directo, unos de ejemplo.
QList<ViewerItem> viewerItems(const OverlaySource *s)
{
	const QJsonArray platforms = s->config.value("platforms").toArray();
	QList<ViewerItem> items;
	for (const QJsonValue v : lastSources) {
		const QJsonObject src = v.toObject();
		const QString platform = src.value("platform").toString();
		if (src.value("live").toInt() == 1 && platform != "demo" && platforms.contains(platform))
			items.append({platform, src.value("channel").toString(), src.value("viewers").toInt(-1)});
	}
	if (items.isEmpty() && s->config.value("preview").toBool() && !streaming) {
		const ViewerItem samples[] = {{"twitch", "micanal", 1234},
					      {"youtube", "@micanal", 856},
					      {"kick", "micanal", 312},
					      {"tiktok", "@micanal", 2045}};
		for (const ViewerItem &sample : samples)
			if (platforms.contains(sample.platform))
				items.append(sample);
	}
	return items;
}

// El total se muestra si hay más de un canal y al menos uno con espectadores conocidos.
int viewerTotal(const OverlaySource *s, const QList<ViewerItem> &items)
{
	if (!s->config.value("total").toBool() || items.size() < 2)
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

// Lo que suma el borde del recuadro a cada dimensión (los estilos con borde propio usan hasta 2 px).
double boxBorder(const OverlaySource *s)
{
	if (!s->config.value("background").toBool())
		return 0;
	return 2.0 * std::max(2, s->config.value("bgBorderWidth").toInt());
}

// Tamaño del recuadro con las medidas de viewers.html (en em = tamaño de letra). La página
// luego escala el recuadro para que llene la fuente justo, así que basta con aproximarlo.
QSize viewerBoxSize(const OverlaySource *s, const QList<ViewerItem> &items, int total)
{
	const double em = std::max(1, s->config.value("size").toInt(28));
	QFont font;
	font.setFamilies({"Segoe UI", ".AppleSystemUIFont", "Helvetica Neue", "Roboto"});
	font.setPixelSize(int(em));
	font.setWeight(QFont::Bold);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	font.setFeature(QFont::Tag("tnum"), 1); // cifras de ancho fijo, como font-variant-numeric
#endif
	QFont small = font;
	small.setPixelSize(std::max(1, int(em * 0.6)));
	small.setWeight(QFont::DemiBold);
	const QFontMetricsF big(font), little(small);
	const bool names = s->config.value("names").toBool();
	const bool column = s->config.value("layout").toString() == "column";
	const double padX = 2.0 * s->config.value("padX").toInt(17), padY = 2.0 * s->config.value("padY").toInt(10);

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
	const double border = boxBorder(s);
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
	const QList<ViewerItem> items = viewerItems(s);
	const int total = viewerTotal(s, items);
	// Sin nada que mostrar la primera vez, el tamaño de cuatro canales de ejemplo.
	const QList<ViewerItem> measured =
		!items.isEmpty() || s->sized ? items
					     : QList<ViewerItem>{{"twitch", "micanal", 1234}, {"youtube", "@micanal", 856},
								 {"kick", "micanal", 312}, {"tiktok", "@micanal", 2045}};
	if (!measured.isEmpty()) {
		s->sized = true;
		const QSize size = viewerBoxSize(s, measured, items.isEmpty() ? viewerTotal(s, measured) : total);
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
QSize clockBoxSize(const OverlaySource *s)
{
	const QJsonObject &c = s->config;
	const double em = std::max(1, c.value("size").toInt(32));
	QFont font;
	font.setFamilies({"Segoe UI", ".AppleSystemUIFont", "Helvetica Neue", "Roboto"});
	font.setPixelSize(int(em));
	font.setWeight(QFont::ExtraBold);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	font.setFeature(QFont::Tag("tnum"), 1);
#endif
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
	const double border = boxBorder(s);
	return QSize(std::min(4096, int(std::ceil(w + border))), int(std::ceil(em * 1.2 + 2.0 * c.value("padY").toInt(11) + border)));
}

void refreshClock(OverlaySource *s)
{
	const QSize size = clockBoxSize(s);
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
	QJsonObject config = s->config;
	config.insert("streaming", streaming.load());
	send(s, QJsonObject{{"type", "config"}, {"config", config}});
}

// Tamaño de la cinta horizontal: el ancho del lienzo y el alto justo de una línea. La línea
// mide 1,35 × la letra (+10 px de relleno con fondo) y se deja un margen pequeño arriba y abajo.
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
	const double line = size * 1.35 + (config.value("background").toBool()
						   ? 2.0 * config.value("msgPadY").toInt() + 2 * border
						   : 0);
	return uint32_t(std::max(10.0, std::ceil(line + 2.0 * config.value("padY").toInt())));
}

// Cinta horizontal con "ajustar al lienzo": su tamaño lo pone el plugin.
bool fitsCanvas(const OverlaySource *s)
{
	return s->kind == Kind::Chat && s->config.value("layout").toString() == "horizontal" && s->config.value("fit").toBool();
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
	char *page = obs_module_file(s->kind == Kind::Viewers ? "overlay/viewers.html"
				     : s->kind == Kind::Clock ? "overlay/clock.html"
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

void boxStyleDefaults(obs_data_t *settings, const std::string &prefix, int opacity, int radius)
{
	const std::string color = prefix.empty() ? "bg_color" : prefix + "color";
	obs_data_set_default_int(settings, color.c_str(), 0xFF000000);
	obs_data_set_default_int(settings, (prefix + "opacity").c_str(), opacity);
	obs_data_set_default_int(settings, (prefix + "radius").c_str(), radius);
	obs_data_set_default_int(settings, (prefix + "border_width").c_str(), 0);
	obs_data_set_default_int(settings, (prefix + "border_color").c_str(), 0xFFFFFFFF);
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

// Los controles del fondo solo se ven con su casilla activada.
bool box_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	showBoxStyle(props, settings, "", obs_data_get_bool(settings, "background"));
	return true;
}

void readSettings(OverlaySource *s, obs_data_t *settings)
{
	if (s->kind == Kind::Chat) { // espectadores y reloj calculan su tamaño solos
		s->width = (uint32_t)std::max<long long>(30, obs_data_get_int(settings, "width"));
		s->height = (uint32_t)std::max<long long>(30, obs_data_get_int(settings, "height"));
	}
	QJsonArray platforms;
	for (const char *p : {"twitch", "youtube", "kick", "tiktok"})
		if (obs_data_get_bool(settings, (std::string("show_") + p).c_str()))
			platforms.append(p);
	if (s->kind == Kind::Clock) {
		s->config = QJsonObject{
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
		readBoxStyle(s->config, settings, "", "bg");
		return;
	}
	if (s->kind == Kind::Viewers) {
		s->config = QJsonObject{
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
		readBoxStyle(s->config, settings, "", "bg");
		return;
	}
	s->config = QJsonObject{
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
		{"fit", obs_data_get_bool(settings, "fit_canvas")},
		{"preview", obs_data_get_bool(settings, "preview")},
	};
	readBoxStyle(s->config, settings, "", "bg");
	readBoxStyle(s->config, settings, "panel_", "panel");
	if (fitsCanvas(s)) {
		s->width = canvasWidth();
		s->height = tickerHeight(s->config);
	}
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
		obs_log(LOG_WARNING, "no se pudo crear la fuente de navegador (¿falta obs-browser?)");
	std::lock_guard<std::mutex> lock(sourcesMutex);
	sources.insert(s);
	sendConfig(s);
	refresh(s);
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
	std::lock_guard<std::mutex> lock(sourcesMutex);
	const bool wasFitting = fitsCanvas(s);
	readSettings(s, settings);
	resizeBrowser(s);
	if (!wasFitting && fitsCanvas(s))
		fitAllScenes(s);
	sendConfig(s);
	refresh(s);
}

void ov_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 500);
	obs_data_set_default_int(settings, "height", 600);
	obs_data_set_default_int(settings, "font_size", 22);
	obs_data_set_default_int(settings, "fade", 0);
	obs_data_set_default_int(settings, "max", 15);
	obs_data_set_default_bool(settings, "show_twitch", true);
	obs_data_set_default_bool(settings, "show_youtube", true);
	obs_data_set_default_bool(settings, "show_kick", true);
	obs_data_set_default_bool(settings, "show_tiktok", true);
	obs_data_set_default_bool(settings, "icons", true);
	obs_data_set_default_bool(settings, "badges", true);
	obs_data_set_default_bool(settings, "background", false);
	obs_data_set_default_string(settings, "style", "solid");
	boxStyleDefaults(settings, "", 60, 9);
	obs_data_set_default_bool(settings, "panel", false);
	boxStyleDefaults(settings, "panel_", 50, 0);
	obs_data_set_default_string(settings, "align", "bottom");
	obs_data_set_default_string(settings, "layout", "vertical");
	obs_data_set_default_int(settings, "speed", 120);
	obs_data_set_default_string(settings, "scroll", "continuous");
	obs_data_set_default_bool(settings, "fit_canvas", true);
	obs_data_set_default_int(settings, "spacing", 5);
	obs_data_set_default_int(settings, "hspacing", 48);
	obs_data_set_default_int(settings, "pad_y", 6);
	obs_data_set_default_int(settings, "pad_x", 10);
	obs_data_set_default_int(settings, "msg_pad_y", 5);
	obs_data_set_default_int(settings, "msg_pad_x", 11);
	obs_data_set_default_bool(settings, "preview", true);
}

void vw_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "font_size", 28);
	obs_data_set_default_string(settings, "layout", "row");
	obs_data_set_default_bool(settings, "total", true);
	obs_data_set_default_bool(settings, "names", false);
	obs_data_set_default_int(settings, "pad_y", 10);
	obs_data_set_default_int(settings, "pad_x", 17);
	for (const char *p : {"show_twitch", "show_youtube", "show_kick", "show_tiktok"})
		obs_data_set_default_bool(settings, p, true);
	obs_data_set_default_bool(settings, "background", true);
	obs_data_set_default_string(settings, "style", "solid");
	boxStyleDefaults(settings, "", 60, 12);
	obs_data_set_default_bool(settings, "preview", true);
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
	obs_property_set_visible(obs_properties_get(props, "scroll"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "clock"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "speed"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "hspacing"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "spacing"), !horizontal);
	obs_property_set_visible(obs_properties_get(props, "align"), !horizontal);
	obs_property_set_visible(obs_properties_get(props, "fade"), !horizontal);
	obs_property_set_visible(obs_properties_get(props, "max"), !horizontal);
	showBoxStyle(props, settings, "", obs_data_get_bool(settings, "background"));
	showBoxStyle(props, settings, "panel_", obs_data_get_bool(settings, "panel"));
	for (const char *key : {"msg_pad_y", "msg_pad_x"})
		obs_property_set_visible(obs_properties_get(props, key), obs_data_get_bool(settings, "background"));
	return true;
}

bool ov_open_settings(obs_properties_t *, obs_property_t *, void *)
{
	open_multichat_settings_from_source();
	return false;
}

obs_properties_t *ov_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_button2(props, "chats", obs_module_text("Source.Chats"), ov_open_settings, nullptr);

	obs_property_t *layout = obs_properties_add_list(props, "layout", obs_module_text("Source.Layout"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(layout, obs_module_text("Source.Layout.Vertical"), "vertical");
	obs_property_list_add_string(layout, obs_module_text("Source.Layout.Horizontal"), "horizontal");
	obs_property_set_long_description(layout, obs_module_text("Source.Layout.Help"));
	obs_property_set_modified_callback(layout, ov_layout_changed);
	obs_property_t *fit = obs_properties_add_bool(props, "fit_canvas", obs_module_text("Source.FitCanvas"));
	obs_property_set_long_description(fit, obs_module_text("Source.FitCanvas.Help"));
	obs_property_set_modified_callback(fit, ov_layout_changed);
	obs_property_t *scroll = obs_properties_add_list(props, "scroll", obs_module_text("Source.Scroll"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(scroll, obs_module_text("Source.Scroll.Continuous"), "continuous");
	obs_property_list_add_string(scroll, obs_module_text("Source.Scroll.Message"), "message");
	obs_properties_add_bool(props, "clock", obs_module_text("Source.Clock"));
	obs_property_t *speed =
		obs_properties_add_int_slider(props, "speed", obs_module_text("Source.Speed"), 20, 600, 10);
	obs_property_int_set_suffix(speed, " px/s");
	obs_property_t *spacing =
		obs_properties_add_int_slider(props, "spacing", obs_module_text("Source.Spacing"), -40, 60, 1);
	obs_property_int_set_suffix(spacing, " px");
	obs_property_t *hspacing =
		obs_properties_add_int_slider(props, "hspacing", obs_module_text("Source.Spacing"), -200, 400, 2);
	obs_property_int_set_suffix(hspacing, " px");
	addPadding(props, "pad_y", "pad_x", "Source.PadY", "Source.PadX");

	obs_property_t *size =
		obs_properties_add_int_slider(props, "font_size", obs_module_text("Source.FontSize"), 10, 72, 1);
	obs_property_int_set_suffix(size, " px");
	obs_property_t *fade = obs_properties_add_int(props, "fade", obs_module_text("Source.Fade"), 0, 600, 1);
	obs_property_int_set_suffix(fade, " s");
	obs_property_set_long_description(fade, obs_module_text("Source.Fade.Help"));
	obs_properties_add_int(props, "max", obs_module_text("Source.Max"), 1, 100, 1);

	obs_properties_add_bool(props, "show_twitch", obs_module_text("Source.ShowTwitch"));
	obs_properties_add_bool(props, "show_youtube", obs_module_text("Source.ShowYouTube"));
	obs_properties_add_bool(props, "show_kick", obs_module_text("Source.ShowKick"));
	obs_properties_add_bool(props, "show_tiktok", obs_module_text("Source.ShowTikTok"));
	obs_properties_add_bool(props, "icons", obs_module_text("Source.Icons"));
	obs_properties_add_bool(props, "badges", obs_module_text("Source.Badges"));
	addStyleList(props);
	obs_property_set_modified_callback(obs_properties_get(props, "style"), ov_layout_changed);
	obs_property_set_modified_callback(
		obs_properties_add_bool(props, "background", obs_module_text("Source.Background")), ov_layout_changed);
	addBoxStyle(props, "");
	addPadding(props, "msg_pad_y", "msg_pad_x", "Source.MsgPadY", "Source.MsgPadX");
	obs_property_set_modified_callback(obs_properties_add_bool(props, "panel", obs_module_text("Source.Panel")),
					   ov_layout_changed);
	addBoxStyle(props, "panel_");

	obs_property_t *align = obs_properties_add_list(props, "align", obs_module_text("Source.Align"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(align, obs_module_text("Source.Align.Bottom"), "bottom");
	obs_property_list_add_string(align, obs_module_text("Source.Align.Top"), "top");

	obs_properties_add_bool(props, "preview", obs_module_text("Source.Preview"));
	obs_properties_add_int(props, "width", obs_module_text("Source.Width"), 100, 4096, 1);
	obs_properties_add_int(props, "height", obs_module_text("Source.Height"), 100, 4096, 1);
	return props;
}

obs_properties_t *vw_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_button2(props, "chats", obs_module_text("Source.Chats"), ov_open_settings, nullptr);

	obs_property_t *layout = obs_properties_add_list(props, "layout", obs_module_text("Viewers.Layout"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(layout, obs_module_text("Viewers.Layout.Row"), "row");
	obs_property_list_add_string(layout, obs_module_text("Viewers.Layout.Column"), "column");
	obs_property_t *size =
		obs_properties_add_int_slider(props, "font_size", obs_module_text("Source.FontSize"), 10, 120, 1);
	obs_property_int_set_suffix(size, " px");
	obs_properties_add_bool(props, "total", obs_module_text("Viewers.Total"));
	obs_properties_add_bool(props, "names", obs_module_text("Viewers.Names"));

	obs_properties_add_bool(props, "show_twitch", obs_module_text("Source.ShowTwitch"));
	obs_properties_add_bool(props, "show_youtube", obs_module_text("Source.ShowYouTube"));
	obs_properties_add_bool(props, "show_kick", obs_module_text("Source.ShowKick"));
	obs_properties_add_bool(props, "show_tiktok", obs_module_text("Source.ShowTikTok"));
	addStyleList(props);
	obs_property_set_modified_callback(obs_properties_get(props, "style"), box_changed);
	obs_property_set_modified_callback(
		obs_properties_add_bool(props, "background", obs_module_text("Viewers.Background")), box_changed);
	addBoxStyle(props, "");
	addPadding(props, "pad_y", "pad_x", "Box.PadY", "Box.PadX");

	obs_properties_add_bool(props, "preview", obs_module_text("Viewers.Preview"));
	return props;
}

const char *ck_get_name(void *)
{
	return obs_module_text("Clock.Name");
}

void ck_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "font_size", 32);
	obs_data_set_default_bool(settings, "date", true);
	obs_data_set_default_bool(settings, "time", true);
	obs_data_set_default_bool(settings, "seconds", false);
	obs_data_set_default_bool(settings, "hour24", true);
	obs_data_set_default_bool(settings, "bar", true);
	obs_data_set_default_int(settings, "pad_y", 11);
	obs_data_set_default_int(settings, "pad_x", 19);
	obs_data_set_default_bool(settings, "background", false);
	obs_data_set_default_string(settings, "style", "solid");
	boxStyleDefaults(settings, "", 60, 12);
}

obs_properties_t *ck_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *size =
		obs_properties_add_int_slider(props, "font_size", obs_module_text("Source.FontSize"), 10, 160, 1);
	obs_property_int_set_suffix(size, " px");
	obs_properties_add_bool(props, "date", obs_module_text("Clock.Date"));
	obs_properties_add_bool(props, "time", obs_module_text("Clock.Time"));
	obs_properties_add_bool(props, "seconds", obs_module_text("Clock.Seconds"));
	obs_properties_add_bool(props, "hour24", obs_module_text("Clock.Hour24"));
	obs_properties_add_bool(props, "bar", obs_module_text("Clock.Bar"));
	addStyleList(props);
	obs_property_set_modified_callback(obs_properties_get(props, "style"), box_changed);
	obs_property_set_modified_callback(
		obs_properties_add_bool(props, "background", obs_module_text("Viewers.Background")), box_changed);
	addBoxStyle(props, "");
	addPadding(props, "pad_y", "pad_x", "Box.PadY", "Box.PadX");
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
	std::lock_guard<std::mutex> lock(sourcesMutex);
	for (OverlaySource *s : sources) {
		sendConfig(s);
		refresh(s); // al transmitir se quitan los números de ejemplo
	}
}

void register_overlay_source()
{
	ChatManager &manager = ChatManager::instance();
	QObject::connect(&manager, &ChatManager::message, &manager,
			 [](const QJsonObject &m) { sendAll(QJsonObject{{"type", "message"}, {"message", m}}); });
	QObject::connect(&manager, &ChatManager::cleared, &manager, [] { sendAll(QJsonObject{{"type", "clear"}}); });
	auto pushViewers = [] {
		const QJsonArray list = ChatManager::instance().state().value("sources").toArray();
		std::lock_guard<std::mutex> lock(sourcesMutex);
		lastSources = list;
		for (OverlaySource *s : sources)
			if (s->kind == Kind::Viewers)
				refreshViewers(s);
	};
	QObject::connect(&manager, &ChatManager::stateChanged, &manager, pushViewers);

	// La página puede recargarse (al cambiar el tamaño, al volver a la escena...):
	// le reenviamos la configuración cada pocos segundos.
	auto *heartbeat = new QTimer(&manager);
	heartbeat->setInterval(3000);
	QObject::connect(heartbeat, &QTimer::timeout, &manager, [] {
		std::lock_guard<std::mutex> lock(sourcesMutex);
		for (OverlaySource *s : sources)
			sendConfig(s);
	});
	QObject::connect(heartbeat, &QTimer::timeout, &manager, pushViewers);
	// La cinta horizontal sigue al lienzo si cambia su resolución.
	QObject::connect(heartbeat, &QTimer::timeout, &manager, [] {
		const uint32_t canvas = canvasWidth();
		std::lock_guard<std::mutex> lock(sourcesMutex);
		for (OverlaySource *s : sources)
			if (fitsCanvas(s) && s->width != canvas) {
				s->width = canvas;
				resizeBrowser(s);
			}
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
}
