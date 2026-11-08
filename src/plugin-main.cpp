/*
Multichat para OBS
Copyright (C) 2026 Alejandro (Alejoxgaming)

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QMetaObject>

#include "chat-manager.hpp"
#include "i18n.hpp"
#include "overlay/overlay-source.hpp"
#include "ui/chat-dock.hpp"
#include "ui/first-run-wizard.hpp"
#include "ui/image-cache.hpp"
#include "ui/profile-cache.hpp"
#include "ui/ui-common.hpp"
#include "updater.hpp"
#include "tts/voice-source.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

// ---- Atajos de teclado (Ajustes → Atajos) ----

obs_hotkey_id clearFeaturedHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id reshowFeaturedHotkey = OBS_INVALID_HOTKEY_ID;

QString configPath(const char *file)
{
	char *path = obs_module_config_path(file);
	QString out = QString::fromUtf8(path);
	bfree(path);
	return out;
}

// The chat manager talks to OBS only through these: config.json, the locale and the log.
ChatManager::Environment managerEnvironment()
{
	ChatManager::Environment env;
	env.store = ConfigStore(configPath("config.json"));
	env.translate = i18n::translator();
	env.log = [](bool warning, const QString &line) {
		obs_log(warning ? LOG_WARNING : LOG_INFO, "%s", line.toUtf8().constData());
	};
	return env;
}

// Las teclas elegidas se guardan junto a la configuración del plugin, no en la colección de escenas.
QString hotkeysPath()
{
	return configPath("hotkeys.json");
}

// OBS llama a los atajos desde su hilo de atajos: el destacado vive en el hilo de la interfaz.
void on_clear_featured(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		QMetaObject::invokeMethod(
			&ChatManager::instance(), [] { ChatManager::instance().clearFeatured(); }, Qt::QueuedConnection);
}

void on_reshow_featured(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		QMetaObject::invokeMethod(
			&ChatManager::instance(), [] { ChatManager::instance().reshowLast(); }, Qt::QueuedConnection);
}

void loadHotkey(obs_data_t *data, const char *key, obs_hotkey_id id)
{
	if (obs_data_array_t *bindings = obs_data_get_array(data, key)) {
		obs_hotkey_load(id, bindings);
		obs_data_array_release(bindings);
	}
}

// false si OBS ya no conoce el atajo (al descargar el plugin ya los ha liberado).
bool saveHotkey(obs_data_t *data, const char *key, obs_hotkey_id id)
{
	obs_data_array_t *bindings = obs_hotkey_save(id);
	if (!bindings)
		return false;
	obs_data_set_array(data, key, bindings);
	obs_data_array_release(bindings);
	return true;
}

void registerHotkeys()
{
	clearFeaturedHotkey = obs_hotkey_register_frontend("multichat.clear_featured",
							    obs_module_text("Hotkey.ClearFeatured"), on_clear_featured,
							    nullptr);
	reshowFeaturedHotkey = obs_hotkey_register_frontend("multichat.reshow_featured",
							     obs_module_text("Hotkey.ReshowFeatured"), on_reshow_featured,
							     nullptr);
	obs_data_t *data = obs_data_create_from_json_file_safe(hotkeysPath().toUtf8().constData(), "bak");
	if (data) {
		loadHotkey(data, "clear_featured", clearFeaturedHotkey);
		loadHotkey(data, "reshow_featured", reshowFeaturedHotkey);
		obs_data_release(data);
	}
}

void saveHotkeys()
{
	if (clearFeaturedHotkey == OBS_INVALID_HOTKEY_ID)
		return;
	obs_data_t *data = obs_data_create();
	// Sin los atajos no se escribe nada: así no se borran las teclas guardadas.
	if (!saveHotkey(data, "clear_featured", clearFeaturedHotkey) ||
	    !saveHotkey(data, "reshow_featured", reshowFeaturedHotkey)) {
		obs_data_release(data);
		return;
	}
	const QString path = hotkeysPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	if (!obs_data_save_json_safe(data, path.toUtf8().constData(), "tmp", "bak"))
		obs_log(LOG_WARNING, "could not save %s", path.toUtf8().constData());
	obs_data_release(data);
}

// Las teclas ya se guardaron al salir (OBS_FRONTEND_EVENT_EXIT): al descargar el plugin OBS ya
// liberó los atajos, así que aquí solo se sueltan los ids.
void unregisterHotkeys()
{
	if (clearFeaturedHotkey == OBS_INVALID_HOTKEY_ID)
		return;
	obs_hotkey_unregister(clearFeaturedHotkey);
	obs_hotkey_unregister(reshowFeaturedHotkey);
	clearFeaturedHotkey = reshowFeaturedHotkey = OBS_INVALID_HOTKEY_ID;
}

// OBS guarda la colección de escenas al cambiar los ajustes (también los atajos) y al salir:
// aprovechamos para guardar las teclas.
void on_frontend_save(obs_data_t *, bool saving, void *)
{
	if (saving) {
		saveHotkeys();
		voice_save_state();
	}
}

void shutdown()
{
	// Primero la voz: así ningún mensaje de los últimos que entreguen los chats se pone a la cola.
	voice_source_shutdown();
	ChatManager::instance().shutdown();
	ImageCache::instance().shutdown();
	ProfileCache::instance().shutdown();
	Updater::instance().stop();
}

void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		ChatManager::instance().setStreaming(true, ui::streamingServiceName());
		overlay_set_streaming(true);
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		ChatManager::instance().setStreaming(false);
		overlay_set_streaming(false);
		break;
	// The voice output lives in the scene collection: released before OBS clears it (switching
	// collections or exiting) and adopted or created again once the next one is loaded.
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		voice_collection_unloading();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		voice_collection_loaded();
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		voice_collection_loaded();
		if (obs_frontend_streaming_active()) {
			ChatManager::instance().setStreaming(true, ui::streamingServiceName());
			overlay_set_streaming(true);
		}
		// First run: guide the user once OBS's window is ready.
		if (first_run_wizard_wanted())
			QMetaObject::invokeMethod(
				&ChatManager::instance(),
				[] { open_first_run_wizard(static_cast<QWidget *>(obs_frontend_get_main_window())); },
				Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		saveHotkeys();
		// Los hilos de los chats terminan antes de que Qt desarme la interfaz.
		shutdown();
		break;
	default:
		break;
	}
}

} // namespace

void open_multichat_settings_from_source()
{
	open_multichat_settings(static_cast<QWidget *>(obs_frontend_get_main_window()));
}

bool obs_module_load(void)
{
	ChatManager::install(managerEnvironment());
	ChatManager::instance().load();
	register_overlay_source();
	register_voice_source();

	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_add_dock_by_id("multichat_dock", obs_module_text("Dock.Title"), new ChatDock(main_window));
	// One entry: the settings window holds the assistant, the update check and "add sources".
	obs_frontend_add_tools_menu_item(
		obs_module_text("Menu.Settings"), [](void *) { open_multichat_settings_from_source(); }, nullptr);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	obs_frontend_add_save_callback(on_frontend_save, nullptr);
	registerHotkeys();
	Updater::instance().start();

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	obs_frontend_remove_save_callback(on_frontend_save, nullptr);
	unregisterHotkeys();
	shutdown();
	obs_log(LOG_INFO, "plugin unloaded");
}
