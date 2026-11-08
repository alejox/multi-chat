<div align="center">

# 💬 Multichat para OBS

**Los chats de Twitch, YouTube, Kick, TikTok y Starvios juntos en un dock de OBS y en tu escena. Detecta cuándo haces multistream y conecta solo los chats de las plataformas donde estás en directo.**

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
| 📡 **Detección de multistream** | Al pulsar *Iniciar transmisión* comprueba en qué plataformas estás en directo y conecta esos chats. El dock avisa con **"Multistream detectado"**. |
| ➕ **Varios chats** | Añade todos los canales que quieras, también varios de la misma plataforma, desde **Herramientas → Multichat…** (o la rueda del dock → *Configurar…*). Al escribir o pegar el canal aparece su foto de perfil para confirmar que es el correcto; si no existe, avisa y pide **Añadir igualmente**. Si no se pudo comprobar (sin conexión o la plataforma no respondió), lo dice y deja añadirlo con **Añadir**. Cada chat se puede pausar sin quitarlo y reordenar arrastrando (el dock sigue ese orden). Para quitar uno, pulsa la ✕ a su derecha. |
| 🎬 **Fuente "Multichat"** | El chat en tu escena, con fondo transparente. En vertical (lista de mensajes) u horizontal (una cinta que pasa de derecha a izquierda). Sus Propiedades muestran lo esencial (Preajuste, Diseño, Plataformas e insignias) y, con *Mostrar opciones avanzadas*, el resto (Texto, Fondo, Avanzado). |
| ✨ **Preajustes y tipografía** | Un clic para cambiar el aspecto: Limpio, Neón, Burbujas, Minimal en una línea, Alto contraste y Vertical 1080×1920. Fuente, grosor, color del texto y del nombre, contorno, sombra e interlineado, y CSS propio para usuarios avanzados. Mira [Aspecto de las fuentes](#-aspecto-de-las-fuentes). |
| 🕒 **Fecha y hora en el chat** | La fuente "Multichat" puede mostrar la fecha y la hora (con o sin segundos, 24 h o 12 h, barra opcional): al final de la cinta en horizontal, o encima o debajo de los mensajes en vertical. La antigua fuente "Multichat (fecha y hora)" sigue funcionando en las escenas que ya la tienen, pero ya no aparece en **Añadir fuente**. |
| 🎨 **Estilos de fondo** | Fondo por mensaje y fondo general (detrás de todo el chat), con color, opacidad, redondeo y borde. Estilos: Sólido, Cristal, Píldora, Ventana macOS, Minimal, Línea, Synthwave, Ciudad neón, Cyberpunk, Retro y Glitch. También en el recuadro de espectadores y en la fecha y hora. |
| 👁 **Fuente "Multichat (espectadores)"** | Un recuadro aparte con el logo y los espectadores de cada canal en directo, y el total. En fila o en columna, con o sin el nombre del canal. |
| ⭐ **Mensaje destacado** | Clic en un mensaje del dock y aparece en grande en pantalla con la fuente **"Multichat (destacado)"**: logo de la plataforma, nombre con su color, insignias, emotes y monto de las donaciones. Entra y sale con una animación y se oculta solo al rato. Clic otra vez o Esc para quitarlo, también con atajos de teclado. |
| 🔊 **Fuente "Multichat (voz)"** | Lee el chat en voz alta como una fuente de audio más, que llega a la emisión y se controla en el mezclador. También puede avisar con un sonido por mensaje (o sonido y lectura) para que te enteres sin mirar la pantalla. |
| 🌐 **Español e inglés** | Todo sigue el idioma de OBS: menús, errores de conexión, mensajes de ejemplo en pantalla y los avisos que arma Multichat (regalos de TikTok; raids, canjes, suscripciones y regalos de Starvios). Los avisos de Twitch y YouTube llegan ya escritos por la plataforma. |
| 🛡️ **Moderación** | Si un moderador borra un mensaje, o banea o expulsa temporalmente a alguien, sus mensajes desaparecen del dock, del chat en pantalla y del destacado. Mira [Moderación y filtros](#-moderación-y-filtros). |
| 🚫 **Filtros** | Oculta bots (Nightbot, StreamElements, Streamlabs… vienen de serie), comandos que empiezan por `!` y palabras bloqueadas, desde la pestaña **Filtros** de **Herramientas → Multichat…**. |
| 🔇 **Silenciar canal** | Clic en un canal del dock para silenciar sus mensajes en Multichat o volver a mostrarlos. No toca la plataforma. |
| 🧪 **Mensajes de prueba** | Un solo interruptor (en la rueda del dock y en Configurar) que llena el dock con un chat de prueba y hace que las fuentes en pantalla muestren mensajes y números de ejemplo mientras no hay chat. Nunca salen mientras transmites. |

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

Compilado en Ubuntu 24.04 para el OBS del [PPA oficial](https://launchpad.net/~obsproject/+archive/ubuntu/obs-studio) (`sudo add-apt-repository ppa:obsproject/obs-studio`); sirve en Ubuntu 24.04 o más nuevo y distribuciones derivadas (Linux Mint 22, Pop!_OS 24.04…). Las fuentes en pantalla usan la fuente de navegador de OBS: el OBS del PPA la trae, el paquete `obs-studio` de los repositorios de Ubuntu no.

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

Al abrir OBS, el plugin revisa si hay una versión nueva en las Releases de GitHub. Si la hay, muestra un aviso con **Actualizar ahora**, **Más tarde** u **Omitir esta versión** (y el dock **Multichat** lo recuerda con su botón). **Actualizar ahora** la descarga, comprueba su suma SHA-256, la instala y pide reiniciar OBS. También se puede revisar a mano con **Buscar actualizaciones**, en **Herramientas → Multichat…** o en la rueda del dock.

Antes de instalar, el plugin compara el archivo descargado con la suma SHA-256 publicada en la Release: la que da GitHub para cada archivo y, si falta, el `SHA256SUMS` que se publica junto a los zips. Si no coincide o no se puede obtener, no instala nada y lo dice; la versión se puede descargar a mano desde la página.

- **Windows:** se instala en `C:\ProgramData\obs-studio\plugins\obs-multichat`, que es de administrador: Windows pide permiso una vez para copiar el plugin. Si estaba en la carpeta de antes (`C:\Program Files\obs-studio`), queda en la nueva y se quita la copia de antes.
- **macOS:** se reemplaza `obs-multichat.plugin` en su carpeta y se le quita la marca de "descargado de internet", así que macOS no vuelve a pedir permiso.
- **Linux:** se reemplaza en `~/.config/obs-studio/plugins/obs-multichat`, sin pedir permisos (descarga con `curl` o, si no está, con `wget`).

Para ver la versión instalada: **Herramientas → Gestor de plugins** (OBS 33).

## 🎛️ Usarlo

La primera vez que abres OBS con el plugin, sin chats configurados, aparece el **asistente de inicio**: la plataforma viene elegida según tu servicio de emisión, añades tu canal, un botón pone el chat, el destacado y la voz en la escena que estás editando (sin duplicarlos si ya están) y al terminar se abre el dock. Se puede saltar y volver a abrir con **Asistente de inicio**, en **Herramientas → Multichat…** o en la rueda del dock.

1. **Herramientas → Multichat…**. Elige la plataforma, escribe el canal y pulsa **Añadir**. También puedes pegar la URL del canal o de un directo de YouTube. Mientras busca el canal se ve una barra de carga; si el nombre tiene un formato que la plataforma no admite, lo dice debajo, y si el canal no existe, el botón pasa a **Añadir igualmente** (Intro nunca añade un canal no encontrado).
   - **Pausar**: desmarca la casilla de un chat para desconectarlo sin perderlo; vuelve a marcarla para reconectarlo.
   - **Ordenar**: arrastra los chats (o selecciona uno y pulsa **Alt+↑ / Alt+↓**). Los chips del dock siguen el mismo orden.
   - **Quitar**: la ✕ de la fila o la tecla **Supr** con el chat seleccionado.
2. **Docks → Multichat** para ver el chat dentro de OBS (el asistente lo abre al terminar).
3. **Añadir fuentes a la escena** (en Configurar) pone de una vez el chat, el destacado y la voz en la escena que editas, solo los que falten. Para hacerlo a mano o para las demás fuentes: **Fuentes → + → Multichat (chat en pantalla)**. Para la cinta horizontal elige *Disposición → Horizontal*. Con *Ajustar al lienzo automáticamente* (activado por defecto) ocupa todo el ancho del lienzo con el alto justo de una línea; desactívalo para poner el tamaño a mano. El *Movimiento* puede ser continuo o por comentario (avanza con cada mensaje y se detiene). Con *Fecha y hora → Mostrar fecha y hora* la cinta termina con la fecha y la hora a la derecha; en vertical van encima o debajo de los mensajes (*Posición en vertical*).
4. Para los espectadores en pantalla: **Fuentes → + → Multichat (espectadores)**.
5. La fecha y la hora son una opción del chat (grupo **Fecha y hora** en sus Propiedades). Las escenas que ya tenían la fuente "Multichat (fecha y hora)" la conservan tal cual; para quitarla, bórrala de la escena y activa la opción en el chat.
6. Para que lea el chat en voz alta: **Fuentes → + → Multichat (voz)**. Mira [Leer el chat en voz alta](#-leer-el-chat-en-voz-alta).
7. Para mostrar en grande un mensaje concreto: **Fuentes → + → Multichat (destacado)**. Mira [Destacar un mensaje](#-destacar-un-mensaje).

### 🧩 El dock

- **Leer con calma**: si subes para leer, la lista no se mueve aunque lleguen mensajes. Aparece la píldora **↓ N nuevos**; clic en ella o **Fin** para volver al final.
- **Clic derecho en un mensaje**: *Destacar en pantalla* / *Quitar destacado*, *Ocultar mensaje*, *Ocultar usuario*, *Copiar mensaje* y *Copiar nombre* (texto plano).
  - *Ocultar mensaje* lo quita del dock, del chat en pantalla y del destacado, igual que un borrado de moderación, pero solo en Multichat: no toca la plataforma. Funciona también con los mensajes de prueba.
  - *Ocultar usuario* lo añade a **Filtros → Ocultar usuarios** y quita sus mensajes de ese chat.
- **Buscar**: **Ctrl+F** (⌘F en macOS) o el botón de la lupa. Filtra por texto, emote o nombre, sin distinguir mayúsculas ni tildes, y dice cuántos coinciden. **Esc** cierra la búsqueda.
- **Hora**: clic derecho → *Mostrar hora* añade la hora a cada mensaje (apagado por defecto; se recuerda).
- **La rueda** (arriba a la derecha): *Configurar…*, *Mensajes de prueba*, *Mostrar hora*, *Tamaño del texto*, *Volver a detectar en qué plataformas estás en directo*, *Asistente de inicio* y *Buscar actualizaciones*.
- **Vaciar el chat**: el botón de la papelera vacía el dock, el chat en pantalla y el destacado. Durante unos segundos aparece **Chat vaciado · Deshacer**; *Deshacer* devuelve los mensajes al dock y al chat en pantalla (en la cinta horizontal, solo los 3 últimos). La voz no los vuelve a leer.
- **Tema**: los íconos y los colores se adaptan al tema de OBS, claro u oscuro.

### ⭐ Destacar un mensaje

1. Añade **Fuentes → + → Multichat (destacado)** a tu escena. Con *Mensajes de prueba* activado muestra un mensaje de ejemplo mientras no hay ninguno destacado, para colocarla (nunca al transmitir).
2. En el dock **Multichat**, haz clic en un mensaje: aparece en la fuente y se resalta en el dock. Otro clic en otro mensaje lo reemplaza.
3. Para quitarlo: clic otra vez en el mensaje resaltado o **Esc** con el dock seleccionado.

En sus **Propiedades** eliges la animación (deslizar, fundido, zoom o ninguna), la posición dentro de la fuente, el tamaño de letra, el ancho y el alto, el fondo con los mismos estilos que el chat y si se ven el logo y las insignias. Con *Se quita solo* (activado por defecto) se queda en pantalla 8 s más 60 ms por carácter, como mucho 30 s; desactívalo para dejarlo hasta que lo quites. El tiempo lo lleva el plugin, no la página: al vencer, el dock deja de resaltar el mensaje a la vez que desaparece de pantalla, y *volver a mostrar el último* sigue funcionando. Si tienes varias fuentes de destacado con tiempos distintos, manda la más corta de las que tengan *Se quita solo* activado; si ninguna lo tiene, el mensaje se queda hasta que lo quites.

En **Ajustes → Atajos** hay dos atajos sin tecla asignada: *Multichat: quitar mensaje destacado* y *Multichat: volver a mostrar el último destacado*. Asígnales la tecla que quieras.

Si la página de la fuente se recarga, el destacado vuelve a aparecer solo si aún no venció su tiempo. Limpiar el chat desde el dock también quita el destacado.

### 🎨 Aspecto de las fuentes

Las Propiedades de las fuentes (chat, destacado y espectadores; también la antigua de fecha y hora) muestran primero lo esencial: **Preajuste**, **Diseño** y **Plataformas**. La casilla **Mostrar opciones avanzadas** (apagada de serie) despliega **Texto**, **Fondo**, **Animación** (destacado) y **Avanzado** (desaparecer después de, máximo de mensajes, espacios, márgenes, fondo general y CSS). Las escenas guardadas siguen igual: no cambió ninguna opción existente, solo dónde se ve.

- **Preajuste** (arriba): elige uno y se ajustan varias opciones a la vez; puedes seguir tocando todo. Si cambias algo de lo que puso el preajuste, la lista vuelve a *Personalizado*.

  | Preajuste | Cómo se ve |
  |---|---|
  | Limpio | Texto blanco con sombra suave, sin recuadros. |
  | Neón | Recuadros synthwave, nombres del color de la plataforma y brillo magenta. |
  | Burbujas | Burbujas oscuras muy redondeadas, sin sombra y con más aire. |
  | Minimal en una línea | En el chat, la cinta horizontal ajustada al lienzo sobre una franja oscura con la fecha y la hora. En las demás, recuadro negro mate. |
  | Alto contraste | Recuadros negros opacos con borde blanco, texto en negrita y nombres amarillos. |
  | Vertical 1080×1920 | Burbujas de cristal grandes para un lienzo vertical (chat de 1000×1400). |

- **Texto**: fuente (las instaladas en el equipo; si falta, se usa la del sistema), grosor, color del texto, color del nombre (el del usuario, el de la plataforma o uno fijo; chat y destacado), contorno, sombra (*Automática*, como antes: solo sin fondo; *Siempre*; *Nunca*) con su color y desenfoque, e interlineado (chat y destacado). El tamaño sigue siendo *Tamaño de letra*.
- **Avanzado → CSS personalizado**: CSS que se añade a la página después de los estilos del plugin (hasta 20 KB). Solo CSS: nunca se ejecuta código. Por ejemplo `.author { text-transform: uppercase; }`.

### 🛡 Moderación y filtros

Cuando los moderadores de una plataforma borran un mensaje o banean a alguien, Multichat lo quita del dock (sin mover la lista si estás leyendo más arriba), del chat en pantalla (con su animación de salida) y del mensaje destacado. El borrado solo afecta al canal donde ocurrió: nunca a los mensajes de otro chat.

| Plataforma | Mensaje borrado | Ban o expulsión temporal | Vaciar el chat |
|---|---|---|---|
| Twitch | ✅ | ✅ | ✅ |
| YouTube | ✅ | ✅ (mensajes ocultados del autor) | — |
| Kick | ✅ | ✅ | ✅ |
| Starvios | ✅ | — | — |
| TikTok | — | — | — |

TikTok no envía, en lo que lee Multichat, ningún aviso de borrado que se pueda relacionar con los mensajes ya mostrados. Starvios solo avisa de mensajes borrados.

En **Herramientas → Multichat… → Filtros** puedes:

- **Ocultar usuarios**: uno por línea. Vienen los bots más comunes (Nightbot, StreamElements, Streamlabs, Moobot, Fossabot, Wizebot, Botrix); el botón *Añadir bots conocidos* los recupera si los borraste.
- **Ocultar comandos**: los mensajes que empiezan por `!` (activado de serie). Las suscripciones, Super Chats, bits y regalos se muestran siempre.
- **Palabras bloqueadas**: una palabra o frase por línea. Se compara la palabra entera, sin distinguir mayúsculas ni tildes: *tonto* oculta «TÓNTO» pero no «tontería».

Los filtros se guardan al momento y se aplican a los mensajes que llegan a partir de ahí; los mensajes ocultos tampoco se leen en voz alta.

### 🔊 Leer el chat en voz alta

Funciona en Windows, macOS y Linux con las voces del sistema, sin servicios externos. En Linux usa **espeak-ng**: la voz es más robótica que las de Windows y macOS.

1. Añade **Fuentes → + → Multichat (voz)** a tu escena.
2. Empieza a leer en cuanto la añades (para silenciarla sin quitarla, baja su volumen en el mezclador). En sus **Propiedades** eliges la voz, la velocidad, si se lee el nombre de quien escribe, qué plataformas se leen, el máximo de caracteres por mensaje y las palabras bloqueadas.
3. El volumen se controla en el **mezclador de audio**, como cualquier otra fuente. Es audio de OBS: llega a la emisión y a la grabación. Para oírlo tú, activa la monitorización en *Propiedades avanzadas de audio*.

Qué se lee y qué no:

- Solo mensajes nuevos, nunca el historial, y solo mientras la fuente está en la escena en emisión. Si sale de ella, se vacía la cola.
- No se leen los emotes, los enlaces ni los emojis. Un mensaje que se queda sin letras ni números se omite.
- Sí se leen los avisos del chat (suscripciones, regalos…). Un `/me` de Twitch se lee como «Ana saluda».
- Un mensaje con una palabra bloqueada no se lee entero.
- Si un mensaje se borra (moderación de la plataforma u *Ocultar mensaje* en el dock) antes de leerse, sale de la cola; si se estaba leyendo, se corta.
- Se leen de uno en uno. Si la cola llega a 20 mensajes, se descarta el más antiguo para no acumular retraso.
- **Al llegar un mensaje** elige qué hace la fuente: *Leerlo en voz alta* (lo de siempre), *Avisar con un sonido* (un aviso corto por mensaje, sin leer, para enterarte sin mirar la pantalla) o *Sonido y luego leerlo*. El aviso viene integrado; puedes poner un WAV propio (PCM de 16 bits, hasta 10 s) y limitar cuántos suenan con *Mínimo entre dos avisos* (por defecto 1,5 s; en el modo mixto solo limita el sonido). Los filtros de plataformas y palabras valen para los tres modos, y un mensaje borrado antes de sonar no suena.
- El botón *Saltar el mensaje actual* corta el que se está leyendo. Al desactivar la fuente se vacía la cola y se detiene la voz.
- Los *Mensajes de prueba* también se leen, para escuchar la voz sin estar en directo, pero nunca mientras transmites.

Límites: el primer mensaje puede tardar unos segundos en sonar mientras el sistema carga la voz, y solo se quitan los enlaces que empiezan por `http://`, `https://` o `www.`, así que `ejemplo.com` se leería.

### Cuándo se conecta cada chat

| Modo | Comportamiento |
|---|---|
| **Automático** (por defecto) | Comprueba cada minuto qué canales están en directo y conecta solo esos chats. Al empezar a transmitir lo comprueba cada 15 s durante 3 minutos. Si no se puede saber el estado de un canal, lo conecta mientras OBS esté transmitiendo. |
| **Siempre conectado** | Conecta todos los chats aunque no estés en directo. En YouTube, TikTok y Starvios espera a que empiece el directo. |

Los chats pausados no se conectan en ningún modo.

Funciona con cualquier forma de hacer multistream (Restream, *Multiple RTMP outputs*, Aitum Multistream…), porque consulta a cada plataforma directamente. Si el servicio de emisión de OBS es Restream, Castr o StreamYard, también se marca como multistream.

### Notas por plataforma

- **Twitch:** conexión IRC anónima, de solo lectura.
- **YouTube:** lee el chat público del directo, sin clave de API. Con `@canal` busca solo el directo activo.
- **Kick:** a veces Kick bloquea la consulta que da el ID del chat. Si el canal queda en error, añádelo con el ID: `micanal#123456`. Para verlo, abre `https://kick.com/api/v2/channels/micanal` en el navegador y busca `"chatroom":{"id":…}`.
- **TikTok:** escribe tu usuario (`@micanal`) o pega la URL. TikTok exige firmar la conexión al chat, así que el plugin usa [Euler Stream](https://www.eulerstream.com), un servicio externo gratuito y sin cuenta que limita las conexiones por día. Si se pasa del límite, el chat queda en error y se reintenta a los 10 minutos. Muestra los comentarios y los regalos.
- **Starvios:** escribe tu usuario (`micanal`) o pega la URL (`https://starvios.com/live/micanal`). No pide cuenta ni clave: el chat se lee de forma anónima y solo de lectura, como lo hace su web, y el plugin no cuenta como espectador. Los mensajes perdidos durante una reconexión se muestran, pero no se leen en voz alta. **Este conector no es oficial** y puede dejar de funcionar si Starvios cambia.

YouTube, Kick, TikTok y Starvios no tienen una API pública oficial para leer el chat sin cuenta: si cambian su web, puede que haya que actualizar el plugin.

## 🛠️ Compilar

El plugin usa la [plantilla oficial de plugins de OBS](https://github.com/obsproject/obs-plugintemplate) actualizada a **OBS 33.0.0**: obs-deps y Qt 6.11 del `2026-10-23`, macOS 13+ universal, Windows x64. La configuración descarga las dependencias sola. En Linux usa el OBS, Qt 6, libcurl y espeak-ng del sistema.

```bash
# macOS (Xcode 26.5+: OBS 33 exige el SDK de macOS 26.5)
cmake --preset macos && cmake --build --preset macos

# Windows (Visual Studio 2022, SDK 10.0.26100)
cmake --preset windows-x64
cmake --build --preset windows-x64

# Ubuntu 24.04 (con el OBS del PPA oficial)
sudo apt install obs-studio qt6-base-dev qt6-base-private-dev libcurl4-openssl-dev libespeak-ng-dev libsimde-dev ninja-build
cmake --preset ubuntu-x86_64 && cmake --build --preset ubuntu-x86_64
```

Pruebas de los parsers (sin OBS ni red, solo Qt 6 Core):

```bash
cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests
```

Cada push compila en GitHub Actions para Windows, macOS y Linux (`.github/workflows/build.yml`). Para publicar una versión, sube `version` en `buildspec.json` y en `data/manifest.json` (CI comprueba que coincidan) y súbelo a `main`: el flujo *Publicar versión* crea el tag `v<versión>` y la Release con los paquetes de los tres sistemas. También se puede empujar el tag a mano o lanzar el flujo desde Actions.

### Estructura

```
src/plugin-main.cpp        carga del plugin, dock, menú y eventos de emisión; conecta el manager con OBS
src/chat-manager.*         detección en vivo / multistream y ciclo de vida de los chats (sin OBS: recibe config, traductor y log)
src/chat-history.*         historial reciente, borrados y "deshacer limpiar"
src/featured-state.*       mensaje destacado: actual, último, copia para deshacer y tiempo en pantalla
src/config-store.*         config.json (con "version") y guardado atómico de los JSON del plugin
src/i18n.*                 textos traducidos (i18n::tr) de data/locale
src/platforms-registry.hpp plataformas: id, nombre, color y orden (data/overlay/icons.js lleva una copia comprobada por las pruebas)
src/overlay/               fuentes "Multichat", "Multichat (espectadores)", "Multichat (destacado)" y la antigua "Multichat (fecha y hora)", obsoleta pero registrada para las escenas guardadas (fuentes de navegador privadas)
  source.cpp               callbacks de OBS y registro de las fuentes
  properties.cpp           ventana de propiedades
  config.cpp               ajustes → configuración que recibe cada página
  viewers-layout.cpp       tamaño de los recuadros y ajuste de la cinta al lienzo
  wiring.cpp               estado compartido y reparto de los eventos del manager a las páginas
src/overlay-presets.*      valores por defecto, preajustes y saneado de CSS/colores/fuentes (sin OBS, con pruebas en tests/test-overlay.cpp)
src/tts/                   fuente "Multichat (voz)": filtro de texto, cola, envío a ritmo real y voz (AVSpeechSynthesizer en macOS, SAPI en Windows, espeak-ng en Linux)
src/chat/                  conectores y parsers: twitch, youtube, kick, tiktok, starvios, demo
src/net/                   red del sistema: WinHTTP (Windows), NSURLSession (macOS) y libcurl (Linux)
src/ui/                    dock (chat-dock, chat-view, dock-prefs), ventana de configuración (settings-dialog, channel-input, chat-list), asistente de inicio, cachés de emotes y de perfiles
data/icons/                íconos SVG monocromos del dock (se tiñen con el color del tema)
src/updater.*              actualizaciones automáticas desde las Releases de GitHub
data/overlay/              páginas del chat (index.html), el destacado (featured.html), los espectadores (viewers.html) y la antigua fuente de fecha y hora (clock.html); icons.js y overlay.js son el código compartido
data/locale/               textos en español e inglés (también los avisos del chat y los errores de conexión, según el idioma de OBS)
tests/                     pruebas sin OBS: parsers (test-chat), voz (test-tts), overlay (test-overlay), manager (test-manager) y JS de las páginas (overlay-js.test.js, con node si está instalado)
```

¿Por qué red nativa y no QtNetwork? OBS 33 no instala los plugins TLS de Qt, así que HTTPS con QtNetwork no funciona dentro de OBS. WinHTTP y NSURLSession soportan HTTPS y WebSocket sin dependencias extra. En Linux, libcurl (que OBS ya usa) hace HTTPS y la conexión TLS del WebSocket; el protocolo WebSocket va en `src/net/net-linux.cpp`.
