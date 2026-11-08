#pragma once

// Internals of the Multichat sources, shared by the files in src/overlay/ (see overlay-source.hpp
// for what the rest of the plugin uses). Everything lives in namespace overlay_source.

#include "overlay-source.hpp"
#include "box-measure.hpp"
#include "../i18n.hpp"
#include "../platforms-registry.hpp"
#include "../chat-manager.hpp"
#include "../overlay-presets.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QCoreApplication>
#include <QThread>
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

namespace overlay_source {

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

// ---- Shared state (defined in wiring.cpp) ----

extern std::mutex sourcesMutex;
extern QJsonArray lastSources;
extern std::mutex featuredMutex;
extern std::set<OverlaySource *> sources;
extern std::atomic<bool> streaming;
extern std::atomic<bool> sampleMessages;
extern QJsonObject featuredState;

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

// ---- wiring.cpp: manager → pages ----
void connectManager();
void send(OverlaySource *s, const QJsonObject &payload);
QJsonObject configOf(const OverlaySource *s);
void sendFeatured(OverlaySource *s);
void sendAll(const QJsonObject &payload);
void refresh(OverlaySource *s);
void sendConfig(OverlaySource *s);

// ---- viewers-layout.cpp: box sizes (measured on the UI thread) and scene fitting ----
QList<ViewerItem> viewerItems(const QJsonObject &config, const QJsonArray &channels);
int viewerTotal(const QJsonObject &config, const QList<ViewerItem> &items);
QJsonObject viewersPayload(const QList<ViewerItem> &items, int total);
void refreshViewers(OverlaySource *s);
void refreshClock(OverlaySource *s);
void requestMeasure(OverlaySource *s);
uint32_t canvasWidth();
uint32_t tickerHeight(const QJsonObject &config);
bool fitsCanvas(Kind kind, const QJsonObject &config);
uint32_t tickerWidth(const QJsonObject &config);
bool fitSceneItem(obs_scene_t *, obs_sceneitem_t *item, void *param);
void fitSceneItems(obs_scene_t *scene, OverlaySource *s);
void fitAllScenes(OverlaySource *s);
obs_data_t *browserSettings(const OverlaySource *s);
void resizeBrowser(OverlaySource *s);

// ---- config.cpp: settings → page config ----
QString hexColor(obs_data_t *settings, const char *key);
void readBoxStyle(QJsonObject &config, obs_data_t *settings, const std::string &prefix, const QString &js);
void applyDefaults(obs_data_t *settings, Kind kind);
void applySetting(obs_data_t *settings, const overlay::Setting &setting);
bool hasSetting(obs_data_t *settings, const overlay::Setting &setting);
bool hasNamesAndLines(Kind kind);
QString cssColorOf(obs_data_t *settings, const char *key, bool withAlpha);
void readTextStyle(QJsonObject &config, obs_data_t *settings, Kind kind);
void addPageTexts(QJsonObject &config, Kind kind);
QJsonObject readConfig(Kind kind, obs_data_t *settings);
QString featuredSourceId(const OverlaySource *s);
void reportFeaturedPolicy(const OverlaySource *s, obs_data_t *settings);
void readSettings(OverlaySource *s, obs_data_t *settings);

// ---- properties.cpp: the properties window ----
void addBoxStyle(obs_properties_t *props, const std::string &prefix);
void addPadding(obs_properties_t *props, const char *keyY, const char *keyX, const char *textY, const char *textX);
void addStyleList(obs_properties_t *props);
void showBoxStyle(obs_properties_t *props, obs_data_t *settings, const std::string &prefix, bool visible);
void addTextStyle(obs_properties_t *props, Kind kind);
void showTextStyle(obs_properties_t *props, obs_data_t *settings);
void showAdvancedGroups(obs_properties_t *props, obs_data_t *settings);
bool box_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings);
bool ov_layout_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings);
void *kindParam(Kind kind);
void addPreset(obs_properties_t *props);
void addCustomCss(obs_properties_t *props);
bool changesVisibility(const char *name);
bool on_modified(void *param, obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
void hookAll(obs_properties_t *props, Kind kind);
bool ov_open_settings(obs_properties_t *, obs_property_t *, void *);
obs_properties_t *addGroup(obs_properties_t *props, const char *name, const char *text);
void addAdvancedSwitch(obs_properties_t *props);
void addPlatforms(obs_properties_t *props);
obs_properties_t *ov_properties(void *);
obs_properties_t *vw_properties(void *);
obs_properties_t *ck_properties(void *);
bool fe_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
obs_properties_t *fe_properties(void *);

// ---- source.cpp: OBS source callbacks ----
const char *ov_get_name(void *);
const char *vw_get_name(void *);
void *create(obs_data_t *settings, obs_source_t *source, Kind kind);
void *ov_create(obs_data_t *settings, obs_source_t *source);
void *vw_create(obs_data_t *settings, obs_source_t *source);
void *ck_create(obs_data_t *settings, obs_source_t *source);
void *fe_create(obs_data_t *settings, obs_source_t *source);
void ov_destroy(void *data);
void ov_update(void *data, obs_data_t *settings);
void ov_defaults(obs_data_t *settings);
void vw_defaults(obs_data_t *settings);
const char *ck_get_name(void *);
void ck_defaults(obs_data_t *settings);
const char *fe_get_name(void *);
void fe_defaults(obs_data_t *settings);
uint32_t ov_width(void *data);
uint32_t ov_height(void *data);
void ov_render(void *data, gs_effect_t *);
void ov_show(void *data);
void ov_hide(void *data);
void ov_activate(void *data);
void ov_deactivate(void *data);
void ov_enum_sources(void *data, obs_source_enum_proc_t enum_callback, void *param);

} // namespace overlay_source
