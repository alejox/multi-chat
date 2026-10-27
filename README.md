# Multichat para OBS

Junta los chats de **Twitch**, **YouTube** y **Kick** en un solo **dock dentro de OBS** y en un **overlay**
para tu escena. Cuando haces **multistream**, detecta en qué plataformas estás en vivo y conecta sus chats automáticamente.

- Se instala como script nativo de OBS (Herramientas → Scripts) y se configura desde ahí.
- Puedes añadir varios chats, incluso varios canales de la misma plataforma.
- No pide claves de API ni iniciar sesión: los chats se leen de forma anónima.
- Solo usa la librería estándar de Python, así que no hay que instalar nada con `pip`.

## Requisitos

- OBS Studio 28 o superior.
- Python 3 configurado en OBS: **Herramientas → Scripts → pestaña "Configuración de Python"**.
  En Windows instala Python 3 de 64 bits desde python.org y apunta OBS a la carpeta de instalación
  (por ejemplo `C:\Users\TU_USUARIO\AppData\Local\Programs\Python\Python312`).

## Instalación

1. Descarga este repositorio y guárdalo en una carpeta fija. El script necesita la carpeta `multichat/` a su lado.
2. En OBS: **Herramientas → Scripts → `+`** y elige `obs_multichat.py`.
3. En la lista **Chats** añade tus canales, uno por línea:
   ```
   twitch:micanal
   youtube:@micanal
   kick:micanal
   ```
   También puedes pegar la URL del canal (`https://kick.com/micanal`) o de un directo concreto de YouTube
   (`https://www.youtube.com/watch?v=XXXXXXXXXXX`).
4. Añade el dock: **Docks → Docks de navegador personalizados…**, con el nombre `Multichat`
   y la URL `http://localhost:8765/dock`.
5. (Opcional) Para mostrar el chat en pantalla, añade una fuente **Navegador** con la URL `http://localhost:8765/overlay`.

## Cómo detecta el multistream

| Modo | Comportamiento |
|---|---|
| **Automático** (por defecto) | Comprueba cada minuto qué canales configurados están en vivo y conecta solo esos chats. Al pulsar *Iniciar transmisión* en OBS lo comprueba cada 15 s durante 3 minutos, para enterarse rápido. Si no se puede saber el estado de un canal, lo conecta igualmente mientras OBS esté transmitiendo. |
| **Siempre conectado** | Conecta todos los chats configurados aunque no estés en directo. En YouTube espera a que empiece el directo. |

El dock muestra el aviso **"Multistream detectado"** cuando hay dos o más plataformas en vivo, o cuando el
servicio de emisión de OBS es un servicio de multistream (Restream, Castr, StreamYard…). Funciona con cualquier
forma de hacer multistream: Restream, el plugin *Multiple RTMP outputs*, Aitum Multistream, etc., porque
consulta directamente a cada plataforma.

## El dock

- Cada chip muestra un canal con su estado: 🟢 conectado, 🟡 conectando o esperando el directo, 🔴 error.
  La etiqueta **LIVE** indica que la plataforma reporta ese canal en directo.
- Haz clic en un chip para ocultar o mostrar los mensajes de ese canal.
- Botones: tamaño de letra, mostrar la hora, volver a detectar, copiar la URL del overlay y limpiar el chat.
- Si subes para leer mensajes anteriores, el chat deja de desplazarse solo y aparece *"↓ Nuevos mensajes"*.

## Opciones del overlay

Añádelas a la URL, por ejemplo `http://localhost:8765/overlay?fade=30&size=24&bg=1`:

| Parámetro | Efecto |
|---|---|
| `fade=30` | Los mensajes desaparecen a los 30 s (`0` = nunca, por defecto) |
| `max=15` | Máximo de mensajes en pantalla |
| `size=20` | Tamaño de letra en px |
| `platforms=twitch,kick` | Muestra solo esas plataformas |
| `icons=0` / `badges=0` | Oculta los iconos de plataforma o las insignias |
| `bg=1` | Fondo semitransparente detrás de cada mensaje |
| `align=top` | Apila los mensajes desde arriba |

## Uso sin OBS (pruebas)

```bash
python -m multichat --demo                     # mensajes de ejemplo para ajustar el overlay
python -m multichat twitch:micanal kick:micanal youtube:@micanal
python -m multichat --config config.example.json
```

## Notas por plataforma

- **Twitch**: conexión IRC anónima. Muestra emotes, insignias, bits, subs y raids.
- **YouTube**: lee el chat público del directo, sin clave de API. Muestra Super Chats, Super Stickers,
  membresías y emojis personalizados. Busca el directo activo del canal (`@handle` o ID `UC…`) automáticamente.
- **Kick**: usa el WebSocket público del chat. A veces Cloudflare bloquea la API de Kick que da el ID del chat;
  si el chip de Kick queda en error, indícalo a mano: `kick:micanal#123456`. Para ver el ID, abre
  `https://kick.com/api/v2/channels/micanal` en el navegador y busca `"chatroom":{"id":…}`.
- **TikTok** todavía no está soportado: su chat necesita un servicio de firma de terceros.

YouTube y Kick no tienen una API pública oficial para leer el chat de forma anónima, así que si cambian su web
puede que haya que actualizar el conector.

## Estructura

```
obs_multichat.py          script de OBS (propiedades, eventos de emisión)
multichat/
  app.py                  une el gestor y el servidor
  manager.py              detección en vivo / multistream y ciclo de vida de los chats
  server.py               servidor HTTP local + Server-Sent Events
  ws.py                   cliente WebSocket mínimo (stdlib)
  platforms/              conectores: twitch, youtube, kick, demo
  web/                    dock.html, overlay.html, common.js
tests/                    pruebas (python -m unittest discover -s tests)
```
