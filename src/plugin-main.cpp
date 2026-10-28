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

#include <QMainWindow>

#include "chat-manager.hpp"
#include "overlay-source.hpp"
#include "ui/chat-dock.hpp"
#include "ui/image-cache.hpp"
#include "ui/profile-cache.hpp"
#include "updater.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

// Nombre del servicio de Ajustes → Emisión (Twitch, YouTube, Restream...).
QString streamingService()
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

void shutdown()
{
	ChatManager::instance().shutdown();
	ImageCache::instance().shutdown();
	ProfileCache::instance().shutdown();
	Updater::instance().stop();
}

void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		ChatManager::instance().setStreaming(true, streamingService());
		overlay_set_streaming(true);
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		ChatManager::instance().setStreaming(false);
		overlay_set_streaming(false);
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (obs_frontend_streaming_active()) {
			ChatManager::instance().setStreaming(true, streamingService());
			overlay_set_streaming(true);
		}
		break;
	case OBS_FRONTEND_EVENT_EXIT:
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
	ChatManager::instance().load();
	register_overlay_source();

	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_add_dock_by_id("multichat_dock", obs_module_text("Dock.Title"), new ChatDock(main_window));
	obs_frontend_add_tools_menu_item(
		obs_module_text("Menu.Settings"), [](void *) { open_multichat_settings_from_source(); }, nullptr);
	obs_frontend_add_tools_menu_item(
		obs_module_text("Update.MenuItem"), [](void *) { Updater::instance().check(true); }, nullptr);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	Updater::instance().start();

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	shutdown();
	obs_log(LOG_INFO, "plugin unloaded");
}
