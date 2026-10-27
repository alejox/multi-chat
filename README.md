<div align="center">

# 💬 Multichat para OBS

**Los chats de Twitch, YouTube y Kick juntos en un dock de OBS y en tu escena. Detecta cuándo haces multistream y conecta solo los chats de las plataformas donde estás en vivo.**

![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4?logo=windows&logoColor=white)
![macOS](https://img.shields.io/badge/macOS-13%2B-000000?logo=apple&logoColor=white)
![OBS](https://img.shields.io/badge/OBS-33-302E31?logo=obsstudio&logoColor=white)

</div>

---

Plugin nativo para OBS Studio 33: no necesita Python, cuentas, claves de API ni servidores aparte.

## ✨ Funciones

| | |
|---|---|
| 🧩 **Dock "Multichat"** | Todos los mensajes juntos con el logo de cada plataforma, insignias (MOD, VIP, SUB, HOST), emotes, Super Chats, bits y suscripciones. |
| 📡 **Detección de multistream** | Al pulsar *Iniciar transmisión* comprueba en qué plataformas estás en vivo y conecta esos chats. El dock avisa con **"Multistream detectado"**. |
| ➕ **Varios chats** | Añade todos los canales que quieras, también varios de la misma plataforma, desde **Herramientas → Multichat: configurar chats**. |
| 🎬 **Fuente "Multichat"** | El chat en tu escena, con fondo transparente. Se configura en sus Propiedades: tamaño, desvanecer, máximo de mensajes, plataformas, fondo y alineación. |
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
3. Para mostrarlo en pantalla: **Fuentes → + → Multichat (chat en pantalla)**.

### Cuándo se conecta cada chat

| Modo | Comportamiento |
|---|---|
| **Automático** (por defecto) | Comprueba cada minuto qué canales están en vivo y conecta solo esos chats. Al empezar a transmitir lo comprueba cada 15 s durante 3 minutos. Si no se puede saber el estado de un canal, lo conecta mientras OBS esté transmitiendo. |
| **Siempre conectado** | Conecta todos los chats aunque no estés en directo. En YouTube espera a que empiece el directo. |

Funciona con cualquier forma de hacer multistream (Restream, *Multiple RTMP outputs*, Aitum Multistream…), porque consulta a cada plataforma directamente. Si el servicio de emisión de OBS es Restream, Castr o StreamYard, también se marca como multistream.

### Notas por plataforma

- **Twitch:** conexión IRC anónima, de solo lectura.
- **YouTube:** lee el chat público del directo, sin clave de API. Con `@canal` busca solo el directo activo.
- **Kick:** a veces Kick bloquea la consulta que da el ID del chat. Si el canal queda en error, añádelo con el ID: `micanal#123456`. Para verlo, abre `https://kick.com/api/v2/channels/micanal` en el navegador y busca `"chatroom":{"id":…}`.
- **TikTok** aún no está soportado: su chat necesita un servicio de firma de terceros.

YouTube y Kick no tienen una API pública oficial para leer el chat sin cuenta: si cambian su web, puede que haya que actualizar el plugin.

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
src/overlay-source.cpp     fuente "Multichat" (fuente de navegador privada)
src/chat/                  conectores y parsers: twitch, youtube, kick, demo
src/net/                   red del sistema: WinHTTP (Windows) y NSURLSession (macOS)
src/ui/                    dock, ventana de configuración, caché de emotes
data/overlay/index.html    página del overlay
data/locale/               textos en español e inglés
```

¿Por qué red nativa y no QtNetwork? OBS 33 no instala los plugins TLS de Qt, así que HTTPS con QtNetwork no funciona dentro de OBS. WinHTTP y NSURLSession soportan HTTPS y WebSocket sin dependencias extra.
