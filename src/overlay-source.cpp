// Fuente "Multichat": muestra el chat en la escena. Por dentro es una fuente de navegador
// privada con data/overlay/index.html; los mensajes le llegan con javascript_event.

#include "overlay-source.hpp"
#include "chat-manager.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <string>

namespace {

struct OverlaySource {
	obs_source_t *self = nullptr;
	obs_source_t *browser = nullptr;
	QJsonObject config;
	uint32_t width = 500;
	uint32_t height = 600;
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

void sendAll(const QJsonObject &payload)
{
	std::lock_guard<std::mutex> lock(sourcesMutex);
	for (OverlaySource *s : sources)
		send(s, payload);
}

void sendConfig(OverlaySource *s)
{
	QJsonObject config = s->config;
	config.insert("streaming", streaming.load());
	send(s, QJsonObject{{"type", "config"}, {"config", config}});
}

obs_data_t *browserSettings(const OverlaySource *s)
{
	obs_data_t *bs = obs_data_create();
	// En Windows obs_module_file puede dar una ruta relativa: la fuente de navegador
	// necesita la absoluta.
	char *page = obs_module_file("overlay/index.html");
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

void readSettings(OverlaySource *s, obs_data_t *settings)
{
	s->width = (uint32_t)std::max<long long>(100, obs_data_get_int(settings, "width"));
	s->height = (uint32_t)std::max<long long>(100, obs_data_get_int(settings, "height"));
	QJsonArray platforms;
	for (const char *p : {"twitch", "youtube", "kick"})
		if (obs_data_get_bool(settings, (std::string("show_") + p).c_str()))
			platforms.append(p);
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
		{"preview", obs_data_get_bool(settings, "preview")},
	};
}

const char *ov_get_name(void *)
{
	return obs_module_text("Source.Name");
}

void *ov_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new OverlaySource();
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
	return s;
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
	if (s->browser) {
		obs_data_t *bs = browserSettings(s);
		obs_source_update(s->browser, bs);
		obs_data_release(bs);
	}
	sendConfig(s);
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
	obs_data_set_default_bool(settings, "icons", true);
	obs_data_set_default_bool(settings, "badges", true);
	obs_data_set_default_bool(settings, "background", false);
	obs_data_set_default_int(settings, "opacity", 60);
	obs_data_set_default_string(settings, "align", "bottom");
	obs_data_set_default_bool(settings, "preview", true);
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
	for (OverlaySource *s : sources)
		sendConfig(s);
}

void register_overlay_source()
{
	ChatManager &manager = ChatManager::instance();
	QObject::connect(&manager, &ChatManager::message, &manager,
			 [](const QJsonObject &m) { sendAll(QJsonObject{{"type", "message"}, {"message", m}}); });
	QObject::connect(&manager, &ChatManager::cleared, &manager, [] { sendAll(QJsonObject{{"type", "clear"}}); });

	// La página puede recargarse (al cambiar el tamaño, al volver a la escena...):
	// le reenviamos la configuración cada pocos segundos.
	auto *heartbeat = new QTimer(&manager);
	heartbeat->setInterval(3000);
	QObject::connect(heartbeat, &QTimer::timeout, &manager, [] {
		std::lock_guard<std::mutex> lock(sourcesMutex);
		for (OverlaySource *s : sources)
			sendConfig(s);
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
}
