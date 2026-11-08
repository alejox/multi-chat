// Fuentes "Multichat" (el chat en la escena), "Multichat (espectadores)", "Multichat (fecha y
// hora)" y "Multichat (destacado)". Por dentro son fuentes de navegador privadas con
// data/overlay/index.html, viewers.html, clock.html y featured.html; los mensajes, los
// espectadores y el mensaje destacado les llegan con javascript_event.

#include "source-internal.hpp"

namespace overlay_source {

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
	if (s->kind == Kind::Featured)
		ChatManager::instance().removeFeaturedAutoHide(featuredSourceId(s));
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

const char *ck_get_name(void *)
{
	return obs_module_text("Clock.Name");
}

void ck_defaults(obs_data_t *settings)
{
	applyDefaults(settings, Kind::Clock);
}

const char *fe_get_name(void *)
{
	return obs_module_text("Featured.Name");
}

void fe_defaults(obs_data_t *settings)
{
	applyDefaults(settings, Kind::Featured);
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

} // namespace overlay_source

using namespace overlay_source;

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
	connectManager();

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

	// Standalone date and time, to place them anywhere in the scene. The chat source can also
	// show them attached to the chat (its "Date and time" group).
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
