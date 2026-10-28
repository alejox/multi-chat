<div align="center">

# 💬 Multichat para OBS

**Los chats de Twitch, YouTube, Kick y TikTok juntos en un dock de OBS y en tu escena. Detecta cuándo haces multistream y conecta solo los chats de las plataformas donde estás en vivo.**

![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4?logo=windows&logoColor=white)
![macOS](https://img.shields.io/badge/macOS-13%2B-000000?logo=apple&logoColor=white)
![OBS](https://img.shields.io/badge/OBS-33-302E31?logo=obsstudio&logoColor=white)

</div>

---

Plugin nativo para OBS Studio 33: no necesita Python, cuentas ni claves de API.

## ✨ Funciones

| | |
|---|---|
| 🧩 **Dock "Multichat"** | Todos los mensajes juntos con el logo de cada plataforma, insignias (MOD, VIP, SUB, HOST), emotes, Super Chats, bits, suscripciones y regalos de TikTok. |
| 👁 **Espectadores** | Cuántas personas te ven ahora en cada plataforma y el total, en el dock y en la ventana de chats. Se actualiza cada 30 s. |
| 📡 **Detección de multistream** | Al pulsar *Iniciar transmisión* comprueba en qué plataformas estás en vivo y conecta esos chats. El dock avisa con **"Multistream detectado"**. |
| ➕ **Varios chats** | Añade todos los canales que quieras, también varios de la misma plataforma, desde **Herramientas → Multichat: configurar chats**. Para quitar uno, pulsa la ✕ a su derecha. |
| 🎬 **Fuente "Multichat"** | El chat en tu escena, con fondo transparente. En vertical (lista de mensajes) u horizontal (una cinta que pasa de derecha a izquierda). Se configura en sus Propiedades: disposición, velocidad, tamaño, desvanecer, máximo de mensajes, plataformas, fondo y alineación. |
| 👁 **Fuente "Multichat (espectadores)"** | Un recuadro aparte con el logo y los espectadores de cada canal en vivo, y el total. En fila o en columna, con o sin el nombre del canal. |
| 🙈 **Filtrar** | Clic en un canal del dock para ocultar o mostrar sus mensajes. |
| 🧪 **Mensajes de prueba** | Para ver el dock y colocar el overlay sin estar en directo. Nunca salen mientras transmites. |

## 📦 Instalar

Descarga el archivo de tu sistema desde **[Releases](../../releases/latest)** y **cierra OBS antes**.

| | Windows (OBS 33) | macOS (OBS 33) |
|---|---|---|
| **Archivo** | `obs-multichat-<versión>-windows-x64.zip` | `obs-multichat-<versión>-macos.zip` |
| **1** | Descomprímelo (clic derecho → **Extraer todo**): quedan las carpetas `obs-plugins` y `data` | Descomprímelo: queda `obs-multichat.plugin` |
| **2** | Copia esas dos carpetas dentro de `C:\Program Files\obs-studio\` y acepta reemplazar y el permiso de administrador | Cópialo a `~/Library/Application Support/obs-studio/plugins/` |
| **3** | — | En la Terminal, una vez: `xattr -cr ~/Library/Application\ Support/obs-studio/plugins/obs-multichat.plugin` |
| **4** | Abre OBS | Abre OBS |

> [!TIP]
> Si ya usabas la versión anterior en Python (`obs_multichat.py`), quítala de **Herramientas → Scripts**.

## 🎛️ Usarlo

1. **Herramientas → Multichat: configurar chats**. Elige la plataforma, escribe el canal y pulsa **Añadir**. También puedes pegar la URL del canal o de un directo de YouTube.
2. **Docks → Multichat** para ver el chat dentro de OBS.
3. Para mostrarlo en pantalla: **Fuentes → + → Multichat (chat en pantalla)**. Para la cinta horizontal elige *Disposición → Horizontal*. Con *Ajustar al lienzo automáticamente* (activado por defecto) ocupa todo el ancho del lienzo con el alto justo de una línea; desactívalo para poner el tamaño a mano. El *Movimiento* puede ser continuo o por comentario (avanza con cada mensaje y se detiene).
4. Para los espectadores en pantalla: **Fuentes → + → Multichat (espectadores)**.

### Cuándo se conecta cada chat

| Modo | Comportamiento |
|---|---|
| **Automático** (por defecto) | Comprueba cada minuto qué canales están en vivo y conecta solo esos chats. Al empezar a transmitir lo comprueba cada 15 s durante 3 minutos. Si no se puede saber el estado de un canal, lo conecta mientras OBS esté transmitiendo. |
| **Siempre conectado** | Conecta todos los chats aunque no estés en directo. En YouTube y TikTok espera a que empiece el directo. |

Funciona con cualquier forma de hacer multistream (Restream, *Multiple RTMP outputs*, Aitum Multistream…), porque consulta a cada plataforma directamente. Si el servicio de emisión de OBS es Restream, Castr o StreamYard, también se marca como multistream.

### Notas por plataforma

- **Twitch:** conexión IRC anónima, de solo lectura.
- **YouTube:** lee el chat público del directo, sin clave de API. Con `@canal` busca solo el directo activo.
- **Kick:** a veces Kick bloquea la consulta que da el ID del chat. Si el canal queda en error, añádelo con el ID: `micanal#123456`. Para verlo, abre `https://kick.com/api/v2/channels/micanal` en el navegador y busca `"chatroom":{"id":…}`.
- **TikTok:** escribe tu usuario (`@micanal`) o pega la URL. TikTok exige firmar la conexión al chat, así que el plugin usa [Euler Stream](https://www.eulerstream.com), un servicio externo gratuito y sin cuenta que limita las conexiones por día. Si se pasa del límite, el chat queda en error y se reintenta a los 10 minutos. Muestra los comentarios y los regalos.

YouTube, Kick y TikTok no tienen una API pública oficial para leer el chat sin cuenta: si cambian su web, puede que haya que actualizar el plugin.

## 🛠️ Compilar

El plugin usa la [plantilla oficial de plugins de OBS](https://github.com/obsproject/obs-plugintemplate) actualizada a **OBS 33.0.0**: obs-deps y Qt 6.11 del `2026-10-23`, macOS 13+ universal, Windows x64. La configuración descarga las dependencias sola.

```bash
# macOS (Xcode 26.5+: OBS 33 exige el SDK de macOS 26.5)
cmake --preset macos && cmake --build --preset macos

# Windows (Visual Studio 2022, SDK 10.0.26100)
cmake --preset windows-x64
cmake --build --preset windows-x64
```

Pruebas de los parsers (sin OBS ni red, solo Qt 6 Core):

```bash
cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests
```

Cada push compila en GitHub Actions para Windows y macOS (`.github/workflows/build.yml`). Para publicar una versión, sube `version` en `buildspec.json` y empuja el tag `v<versión>`.

### Estructura

```
src/plugin-main.cpp        carga del plugin, dock, menú y eventos de emisión
src/chat-manager.*         detección en vivo / multistream y ciclo de vida de los chats
src/overlay-source.cpp     fuentes "Multichat" y "Multichat (espectadores)" (fuentes de navegador privadas)
src/chat/                  conectores y parsers: twitch, youtube, kick, tiktok, demo
src/net/                   red del sistema: WinHTTP (Windows) y NSURLSession (macOS)
src/ui/                    dock, ventana de configuración, caché de emotes
data/overlay/              páginas del chat (index.html) y de los espectadores (viewers.html)
data/locale/               textos en español e inglés
```

¿Por qué red nativa y no QtNetwork? OBS 33 no instala los plugins TLS de Qt, así que HTTPS con QtNetwork no funciona dentro de OBS. WinHTTP y NSURLSession soportan HTTPS y WebSocket sin dependencias extra.
