<div align="center">

# 💬 Multichat para OBS

**Los chats de Twitch, YouTube, Kick y TikTok juntos en un dock de OBS y en tu escena. Detecta cuándo haces multistream y conecta solo los chats de las plataformas donde estás en vivo.**

![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4?logo=windows&logoColor=white)
![macOS](https://img.shields.io/badge/macOS-13%2B-000000?logo=apple&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-Ubuntu%2024.04%2B-E95420?logo=ubuntu&logoColor=white)
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
| ➕ **Varios chats** | Añade todos los canales que quieras, también varios de la misma plataforma, desde **Herramientas → Multichat: configurar chats**. Al escribir o pegar el canal aparece su foto de perfil para confirmar que es el correcto. Para quitar uno, pulsa la ✕ a su derecha. |
| 🎬 **Fuente "Multichat"** | El chat en tu escena, con fondo transparente. En vertical (lista de mensajes) u horizontal (una cinta que pasa de derecha a izquierda). Se configura en sus Propiedades: disposición, velocidad, tamaño, desvanecer, máximo de mensajes, plataformas, fondo y alineación. |
| 🕒 **Fuente "Multichat (fecha y hora)"** | Un recuadro aparte con la fecha y la hora (con o sin segundos, 24 h o 12 h, barra y fondo opcionales), a su tamaño justo. |
| 🎨 **Estilos de fondo** | Fondo por mensaje y fondo general (detrás de todo el chat), con color, opacidad, redondeo y borde. Estilos: Sólido, Cristal, Píldora, Ventana macOS, Minimal, Línea, Synthwave, Ciudad neón, Cyberpunk, Retro y Glitch. También en los recuadros de espectadores y de fecha y hora. |
| 👁 **Fuente "Multichat (espectadores)"** | Un recuadro aparte con el logo y los espectadores de cada canal en vivo, y el total. En fila o en columna, con o sin el nombre del canal. |
| 🙈 **Filtrar** | Clic en un canal del dock para ocultar o mostrar sus mensajes. |
| 🧪 **Mensajes de prueba** | Para ver el dock y colocar el overlay sin estar en directo. Nunca salen mientras transmites. |

## 📦 Instalar

Requiere **OBS Studio 33 o superior** (Windows 10/11, macOS 13+ o Linux x86_64). Con una versión anterior de OBS, el plugin no carga y OBS avisa de que «no se han podido cargar» los plugins: actualiza OBS primero.

Descarga el archivo de tu sistema desde **[Releases](../../releases/latest)** y **cierra OBS antes de instalar**.

### Windows

Desde OBS 33 los plugins se instalan en **`C:\ProgramData\obs-studio\plugins`**, una carpeta por plugin (ya no dentro de `C:\Program Files\obs-studio`).

1. Descarga `obs-multichat-<versión>-windows-x64.zip` y descomprímelo (clic derecho → **Extraer todo**). Queda la carpeta `obs-multichat`.
2. Abre `C:\ProgramData\obs-studio\plugins` (escríbelo en la barra del Explorador; `ProgramData` es una carpeta oculta). Si no existe la carpeta `plugins`, créala.
3. Copia ahí la carpeta `obs-multichat` (si ya estaba, reemplázala) y acepta el permiso de administrador. Debe quedar así:

   ```
   C:\ProgramData\obs-studio\plugins\obs-multichat\
   ├── obs-multichat.dll
   └── data\
       ├── manifest.json
       ├── locale\
       └── overlay\
   ```

4. Si instalaste una versión anterior en `C:\Program Files\obs-studio`, borra `obs-plugins\64bit\obs-multichat.dll` y la carpeta `data\obs-plugins\obs-multichat`: OBS 33 la marca como *Legacy* y OBS 34 ya no la carga.
5. Abre OBS.

### macOS

1. Descarga `obs-multichat-<versión>-macos.zip` y descomprímelo: queda `obs-multichat.plugin`.
2. Cópialo a `~/Library/Application Support/obs-studio/plugins/` (en el Finder: **Ir → Ir a la carpeta…**). Si ya había una versión, reemplázala.
3. En la Terminal, una vez: `xattr -cr ~/Library/Application\ Support/obs-studio/plugins/obs-multichat.plugin`
4. Abre OBS.

### Linux

Compilado en Ubuntu 24.04 para el OBS del [PPA oficial](https://launchpad.net/~obsproject/+archive/ubuntu/obs-studio) (`sudo add-apt-repository ppa:obsproject/obs-studio`); sirve en Ubuntu 24.04 o más nuevo y distribuciones derivadas (Linux Mint 22, Pop!_OS 24.04…). El overlay usa la fuente de navegador de OBS: el OBS del PPA la trae, el paquete `obs-studio` de los repositorios de Ubuntu no.

1. Descarga `obs-multichat-<versión>-linux-x86_64.tar.gz`.
2. Descomprímelo en la carpeta de plugins de tu usuario:

   ```bash
   mkdir -p ~/.config/obs-studio/plugins
   tar -xzf obs-multichat-*-linux-x86_64.tar.gz -C ~/.config/obs-studio/plugins
   ```

   Debe quedar así:

   ```
   ~/.config/obs-studio/plugins/obs-multichat/
   ├── bin/64bit/obs-multichat.so
   └── data/
       ├── manifest.json
       ├── locale/
       └── overlay/
   ```

3. Abre OBS.

### Actualizaciones automáticas

Al abrir OBS, el plugin revisa si hay una versión nueva en las Releases de GitHub. Si la hay, muestra un aviso con **Actualizar ahora**, **Más tarde** u **Omitir esta versión** (y el dock **Multichat** lo recuerda con su botón). **Actualizar ahora** la descarga, la instala y pide reiniciar OBS. También se puede revisar a mano en **Herramientas → Multichat: buscar actualizaciones**.

- **Windows:** se instala en `C:\ProgramData\obs-studio\plugins\obs-multichat`, que es de administrador: Windows pide permiso una vez para copiar el plugin. Si estaba en la carpeta de antes (`C:\Program Files\obs-studio`), queda en la nueva y se quita la copia de antes.
- **macOS:** se reemplaza `obs-multichat.plugin` en su carpeta y se le quita la marca de "descargado de internet", así que macOS no vuelve a pedir permiso.
- **Linux:** se reemplaza en `~/.config/obs-studio/plugins/obs-multichat`, sin pedir permisos (descarga con `curl` o, si no está, con `wget`).

Para ver la versión instalada: **Herramientas → Gestor de plugins** (OBS 33).

## 🎛️ Usarlo

1. **Herramientas → Multichat: configurar chats**. Elige la plataforma, escribe el canal y pulsa **Añadir**. También puedes pegar la URL del canal o de un directo de YouTube.
2. **Docks → Multichat** para ver el chat dentro de OBS.
3. Para mostrarlo en pantalla: **Fuentes → + → Multichat (chat en pantalla)**. Para la cinta horizontal elige *Disposición → Horizontal*. Con *Ajustar al lienzo automáticamente* (activado por defecto) ocupa todo el ancho del lienzo con el alto justo de una línea; desactívalo para poner el tamaño a mano. El *Movimiento* puede ser continuo o por comentario (avanza con cada mensaje y se detiene). Con *Mostrar fecha y hora* la cinta termina con la fecha y la hora a la derecha.
4. Para los espectadores en pantalla: **Fuentes → + → Multichat (espectadores)**.
5. Para la fecha y la hora sueltas: **Fuentes → + → Multichat (fecha y hora)**.

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

El plugin usa la [plantilla oficial de plugins de OBS](https://github.com/obsproject/obs-plugintemplate) actualizada a **OBS 33.0.0**: obs-deps y Qt 6.11 del `2026-10-23`, macOS 13+ universal, Windows x64. La configuración descarga las dependencias sola. En Linux usa el OBS, Qt 6 y libcurl del sistema.

```bash
# macOS (Xcode 26.5+: OBS 33 exige el SDK de macOS 26.5)
cmake --preset macos && cmake --build --preset macos

# Windows (Visual Studio 2022, SDK 10.0.26100)
cmake --preset windows-x64
cmake --build --preset windows-x64

# Ubuntu 24.04 (con el OBS del PPA oficial)
sudo apt install obs-studio qt6-base-dev qt6-base-private-dev libcurl4-openssl-dev libsimde-dev ninja-build
cmake --preset ubuntu-x86_64 && cmake --build --preset ubuntu-x86_64
```

Pruebas de los parsers (sin OBS ni red, solo Qt 6 Core):

```bash
cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests
```

Cada push compila en GitHub Actions para Windows, macOS y Linux (`.github/workflows/build.yml`). Para publicar una versión, sube `version` en `buildspec.json` y en `data/manifest.json` (CI comprueba que coincidan) y súbelo a `main`: el flujo *Publicar versión* crea el tag `v<versión>` y la Release con los paquetes de los tres sistemas. También se puede empujar el tag a mano o lanzar el flujo desde Actions.

### Estructura

```
src/plugin-main.cpp        carga del plugin, dock, menú y eventos de emisión
src/chat-manager.*         detección en vivo / multistream y ciclo de vida de los chats
src/overlay-source.cpp     fuentes "Multichat", "Multichat (espectadores)" y "Multichat (fecha y hora)" (fuentes de navegador privadas)
src/chat/                  conectores y parsers: twitch, youtube, kick, tiktok, demo
src/net/                   red del sistema: WinHTTP (Windows), NSURLSession (macOS) y libcurl (Linux)
src/ui/                    dock, ventana de configuración, cachés de emotes y de perfiles
src/updater.*              actualizaciones automáticas desde las Releases de GitHub
data/overlay/              páginas del chat (index.html), los espectadores (viewers.html) y la fecha y hora (clock.html)
data/locale/               textos en español e inglés
```

¿Por qué red nativa y no QtNetwork? OBS 33 no instala los plugins TLS de Qt, así que HTTPS con QtNetwork no funciona dentro de OBS. WinHTTP y NSURLSession soportan HTTPS y WebSocket sin dependencias extra. En Linux, libcurl (que OBS ya usa) hace HTTPS y la conexión TLS del WebSocket; el protocolo WebSocket va en `src/net/net-linux.cpp`.
