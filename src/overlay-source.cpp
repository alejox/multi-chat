// Fuentes "Multichat" (el chat en la escena) y "Multichat (espectadores)". Por dentro son
// fuentes de navegador privadas con data/overlay/index.html y viewers.html; los mensajes y
// los espectadores les llegan con javascript_event.

#include "overlay-source.hpp"
#include "chat-manager.hpp"

#include <obs-module.h>
#include <plugin-support.h>

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

struct OverlaySource {
	bool viewers = false; // fuente de espectadores en lugar de chat
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

void sendAll(const QJsonObject &payload, bool viewers = false)
{
	std::lock_guard<std::mutex> lock(sourcesMutex);
	for (OverlaySource *s : sources)
		if (s->viewers == viewers)
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
		w += em * 1.2;
		h = em * 0.7 + double(items.size()) * em * 1.2 + double(items.size() - 1) * em * 0.3;
		if (total >= 0)
			h += em * 1.75 + 2;
	} else {
		w = em * 1.2 + em * 0.9 * double(items.size() - 1);
		for (double x : widths)
			w += x;
		if (total >= 0)
			w += em * 1.8 + 2 + totalWidth;
		h = em * 1.9;
	}
	return QSize(std::min(4096, int(std::ceil(w))), std::min(4096, int(std::ceil(h))));
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
	const double line = size * 1.35 + (config.value("background").toBool() ? 10 : 0);
	const double pad = std::max(4.0, std::round(size * 0.2));
	return uint32_t(std::ceil(line + 2 * pad));
}

bool isHorizontal(const OverlaySource *s)
{
	return !s->viewers && s->config.value("layout").toString() == "horizontal";
}

obs_data_t *browserSettings(const OverlaySource *s)
{
	obs_data_t *bs = obs_data_create();
	// En Windows obs_module_file puede dar una ruta relativa: la fuente de navegador
	// necesita la absoluta.
	char *page = obs_module_file(s->viewers ? "overlay/viewers.html" : "overlay/index.html");
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

void readSettings(OverlaySource *s, obs_data_t *settings)
{
	if (!s->viewers) { // la de espectadores calcula su tamaño sola
		s->width = (uint32_t)std::max<long long>(30, obs_data_get_int(settings, "width"));
		s->height = (uint32_t)std::max<long long>(30, obs_data_get_int(settings, "height"));
	}
	QJsonArray platforms;
	for (const char *p : {"twitch", "youtube", "kick", "tiktok"})
		if (obs_data_get_bool(settings, (std::string("show_") + p).c_str()))
			platforms.append(p);
	if (s->viewers) {
		s->config = QJsonObject{
			{"size", (int)obs_data_get_int(settings, "font_size")},
			{"layout", obs_data_get_string(settings, "layout")},
			{"total", obs_data_get_bool(settings, "total")},
			{"names", obs_data_get_bool(settings, "names")},
			{"platforms", platforms},
			{"background", obs_data_get_bool(settings, "background")},
			{"opacity", (int)obs_data_get_int(settings, "opacity")},
			{"preview", obs_data_get_bool(settings, "preview")},
		};
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
		{"opacity", (int)obs_data_get_int(settings, "opacity")},
		{"align", obs_data_get_string(settings, "align")},
		{"layout", obs_data_get_string(settings, "layout")},
		{"speed", (int)obs_data_get_int(settings, "speed")},
		{"spacing", (int)obs_data_get_int(settings, "spacing")},
		{"hspacing", (int)obs_data_get_int(settings, "hspacing")},
		{"preview", obs_data_get_bool(settings, "preview")},
	};
	if (isHorizontal(s)) {
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

void *create(obs_data_t *settings, obs_source_t *source, bool viewers)
{
	auto *s = new OverlaySource();
	s->viewers = viewers;
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
	if (viewers)
		refreshViewers(s);
	return s;
}

void *ov_create(obs_data_t *settings, obs_source_t *source)
{
	return create(settings, source, false);
}

void *vw_create(obs_data_t *settings, obs_source_t *source)
{
	return create(settings, source, true);
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
	readSettings(s, settings);
	resizeBrowser(s);
	sendConfig(s);
	if (s->viewers)
		refreshViewers(s);
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
	obs_data_set_default_int(settings, "opacity", 60);
	obs_data_set_default_string(settings, "align", "bottom");
	obs_data_set_default_string(settings, "layout", "vertical");
	obs_data_set_default_int(settings, "speed", 120);
	obs_data_set_default_int(settings, "spacing", 5);
	obs_data_set_default_int(settings, "hspacing", 48);
	obs_data_set_default_bool(settings, "preview", true);
}

void vw_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "font_size", 28);
	obs_data_set_default_string(settings, "layout", "row");
	obs_data_set_default_bool(settings, "total", true);
	obs_data_set_default_bool(settings, "names", false);
	for (const char *p : {"show_twitch", "show_youtube", "show_kick", "show_tiktok"})
		obs_data_set_default_bool(settings, p, true);
	obs_data_set_default_bool(settings, "background", true);
	obs_data_set_default_int(settings, "opacity", 60);
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
	obs_property_set_visible(obs_properties_get(props, "width"), !horizontal);
	obs_property_set_visible(obs_properties_get(props, "height"), !horizontal);
	obs_property_set_visible(obs_properties_get(props, "speed"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "hspacing"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "spacing"), !horizontal);
	obs_property_set_visible(obs_properties_get(props, "align"), !horizontal);
	obs_property_set_visible(obs_properties_get(props, "fade"), !horizontal);
	obs_property_set_visible(obs_properties_get(props, "max"), !horizontal);
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
	obs_property_t *speed =
		obs_properties_add_int_slider(props, "speed", obs_module_text("Source.Speed"), 20, 600, 10);
	obs_property_int_set_suffix(speed, " px/s");
	obs_property_t *spacing =
		obs_properties_add_int_slider(props, "spacing", obs_module_text("Source.Spacing"), -40, 60, 1);
	obs_property_int_set_suffix(spacing, " px");
	obs_property_t *hspacing =
		obs_properties_add_int_slider(props, "hspacing", obs_module_text("Source.Spacing"), -200, 400, 2);
	obs_property_int_set_suffix(hspacing, " px");

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
	obs_properties_add_bool(props, "background", obs_module_text("Source.Background"));
	obs_property_t *opacity =
		obs_properties_add_int_slider(props, "opacity", obs_module_text("Source.Opacity"), 0, 100, 5);
	obs_property_int_set_suffix(opacity, " %");

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
	obs_properties_add_bool(props, "background", obs_module_text("Viewers.Background"));
	obs_property_t *opacity =
		obs_properties_add_int_slider(props, "opacity", obs_module_text("Source.Opacity"), 0, 100, 5);
	obs_property_int_set_suffix(opacity, " %");

	obs_properties_add_bool(props, "preview", obs_module_text("Viewers.Preview"));
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
		if (s->viewers)
			refreshViewers(s); // al transmitir se quitan los números de ejemplo
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
			if (s->viewers)
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
			if (isHorizontal(s) && s->width != canvas) {
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
}
