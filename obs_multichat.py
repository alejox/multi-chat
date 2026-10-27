"""Multichat para OBS Studio.

Instalación: Herramientas → Scripts → "+" → selecciona este archivo.
(Requiere tener Python configurado en Herramientas → Scripts → Configuración de Python.)
"""
import logging
import os
import sys
import webbrowser

import obspython as obs

_DIR = os.path.dirname(os.path.abspath(__file__))
if _DIR not in sys.path:
    sys.path.insert(0, _DIR)

# Al recargar el script desde OBS, forzamos a volver a importar el paquete.
for _name in [m for m in sys.modules if m == "multichat" or m.startswith("multichat.")]:
    del sys.modules[_name]

from multichat import __version__  # noqa: E402
from multichat.app import DEFAULT_PORT, App  # noqa: E402
from multichat.manager import MODE_ALWAYS, MODE_AUTO  # noqa: E402

app = None


class _ObsLogHandler(logging.Handler):
    def emit(self, record):
        print(self.format(record))  # OBS muestra print() en el registro del script


log = logging.getLogger("multichat")
log.handlers = [_ObsLogHandler()]
log.setLevel(logging.INFO)
log.propagate = False


# -- utilidades OBS ---------------------------------------------------------
def _read_chats(settings):
    arr = obs.obs_data_get_array(settings, "chats")
    chats = []
    for i in range(obs.obs_data_array_count(arr)):
        item = obs.obs_data_array_item(arr, i)
        chats.append(obs.obs_data_get_string(item, "value"))
        obs.obs_data_release(item)
    obs.obs_data_array_release(arr)
    return chats


def _streaming_service():
    """Nombre del servicio configurado en Ajustes → Emisión (Twitch, YouTube, Restream...)."""
    svc = obs.obs_frontend_get_streaming_service()
    if not svc:
        return ""
    data = obs.obs_service_get_settings(svc)
    name = obs.obs_data_get_string(data, "service") or obs.obs_data_get_string(data, "server")
    obs.obs_data_release(data)
    return name


def _on_frontend_event(event):
    if app is None:
        return
    if event == obs.OBS_FRONTEND_EVENT_STREAMING_STARTED:
        app.set_streaming(True, _streaming_service())
    elif event == obs.OBS_FRONTEND_EVENT_STREAMING_STOPPED:
        app.set_streaming(False)


def _open_dock(props, prop):
    if app:
        webbrowser.open(app.url("dock"))
    return False


def _open_overlay(props, prop):
    if app:
        webbrowser.open(app.url("overlay"))
    return False


def _refresh(props, prop):
    if app:
        app.manager.refresh()
    return False


# -- API de scripts de OBS ----------------------------------------------------
def script_description():
    port = app.port if app else DEFAULT_PORT
    err = f'<p style="color:#ff6b6b">{app.server_error}</p>' if app and app.server_error else ""
    return f"""<h3>Multichat v{__version__}</h3>
<p>Une los chats de <b>Twitch</b>, <b>YouTube</b> y <b>Kick</b>. Detecta en qué plataformas
estás en vivo al hacer multistream y conecta sus chats automáticamente.</p>
<p><b>Dock:</b> Docks → Docks de navegador personalizados →
<code>http://localhost:{port}/dock</code><br>
<b>Overlay:</b> fuente Navegador → <code>http://localhost:{port}/overlay</code></p>{err}"""


def script_defaults(settings):
    obs.obs_data_set_default_int(settings, "port", DEFAULT_PORT)
    obs.obs_data_set_default_string(settings, "mode", MODE_AUTO)


def script_properties():
    props = obs.obs_properties_create()
    obs.obs_properties_add_editable_list(
        props, "chats", "Chats (plataforma:canal)", obs.OBS_EDITABLE_LIST_TYPE_STRINGS, None, None)
    obs.obs_properties_add_text(
        props, "help",
        "Ejemplos: twitch:micanal · youtube:@micanal · kick:micanal\n"
        "También puedes pegar la URL del canal o de un directo de YouTube.",
        obs.OBS_TEXT_INFO)
    mode = obs.obs_properties_add_list(
        props, "mode", "Modo", obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING)
    obs.obs_property_list_add_string(mode, "Automático (solo plataformas en vivo)", MODE_AUTO)
    obs.obs_property_list_add_string(mode, "Siempre conectado", MODE_ALWAYS)
    obs.obs_properties_add_int(props, "port", "Puerto local", 1024, 65535, 1)
    obs.obs_properties_add_button(props, "open_dock", "Abrir dock en el navegador", _open_dock)
    obs.obs_properties_add_button(props, "open_overlay", "Abrir overlay en el navegador", _open_overlay)
    obs.obs_properties_add_button(props, "refresh", "Volver a detectar plataformas en vivo", _refresh)
    return props


def script_update(settings):
    if app is None:
        return
    app.apply(_read_chats(settings), obs.obs_data_get_string(settings, "mode") or MODE_AUTO,
              obs.obs_data_get_int(settings, "port") or DEFAULT_PORT)


def script_load(settings):
    global app
    app = App()
    obs.obs_frontend_add_event_callback(_on_frontend_event)
    script_update(settings)
    if obs.obs_frontend_streaming_active():
        app.set_streaming(True, _streaming_service())


def script_unload():
    global app
    if app:
        app.stop()
        app = None
