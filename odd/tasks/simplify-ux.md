# Feature: simplify-ux (Fase A)

## Objective
Quitar decisiones al usuario sin quitar funciones: de ~12 a ~5 decisiones antes de estar en vivo, un solo vocabulario, menos opciones a la vista, y que el destacado y la voz se comporten como uno espera.

## Problem and why
Revisión de equipo (arquitectura + UX + mercado, 2026-11-08) sobre `feat/quality`:
- La voz viene desactivada por defecto (`src/tts/voice-source.cpp:190`): una fuente de audio muda parece rota.
- El auto-ocultar del destacado vive solo en `featured.html`; `ChatManager::featured_` nunca se limpia al expirar: el dock sigue resaltado y el menú ofrece "Quitar destacado" de algo que ya no está en pantalla.
- "Ocultar" nombra seis comportamientos distintos (mensaje, usuario, chip, desvanecer, auto-ocultar, filtros); README llama "Filtrar" al chip.
- El asistente ofrece solo chat + destacado, no muestra el dock ni usa `streamingService()` (`src/plugin-main.cpp:43`) para preseleccionar la plataforma.
- "Mensajes de prueba" existe en 4 sitios (Settings, asistente, por fuente).
- Chat: ~60 propiedades en 7 grupos siempre visibles; los 6 preajustes cubren la mayoría de los casos.
- Herramientas tiene 3 entradas; el dock ya tiene rueda y banner de actualización.
- `Settings.OverlayHelp` manda al usuario a `Fuentes → +` en vez de ofrecer un botón.

## Scope
- T1 Voz: `enabled` por defecto `true` para fuentes nuevas (solo `obs_data_set_default_bool`; fuentes existentes conservan su valor guardado). Ayuda: "Silenciá desde el mezclador".
- T2 Destacado sincronizado: el temporizador de auto-ocultar pasa a C++ (`ChatManager`, QTimer), calculado con los mismos parámetros (base + por carácter + máximo) que hoy decide la página; la página recibe `featuredChanged` vacío al expirar y hace la salida. Si varias fuentes destacadas tienen tiempos distintos, el manager usa el de la fuente con auto-ocultar activado y menor duración (documentar la regla). "Volver a mostrar" sigue funcionando con `lastFeatured_`.
- T3 Vocabulario en `es-ES.ini`, `en-US.ini` y README: *Ocultar* = quitar de Multichat (mensaje/usuario); *Silenciar canal* = chip del dock; *Pausar* = desconectar; *Filtros* = reglas; `Source.Fade` → "Desaparecen después de"; `Featured.AutoHide` → "Se quita solo"; "canal" para lo que se añade, "chat" para los mensajes, "en pantalla" en vez de "overlay"; registros uniformes en errores; traducir `Live.No`, `Text.Weight.900`. Lista de claves tocadas en el documento.
- T4 Mensajes de prueba globales: un solo interruptor (`ChatManager`, persistido en `config.json`), expuesto en Configurar y en la rueda del dock; las fuentes lo leen de la config que ya reciben y se elimina `preview` por fuente (se sigue leyendo la clave vieja una versión para no romper escenas: si está en true, cuenta como global activado la primera vez y se migra).
- T5 Propiedades: orden Preajuste → Diseño → Plataformas; un bool `advanced` ("Mostrar opciones avanzadas", por defecto off) que muestra Texto, Fondo, Animación y Avanzado (patrón `changesVisibility` existente). Mover a Avanzado: fade, max, pad, spacing, panel. Mismo criterio en las 4 fuentes. Sin renombrar claves.
- T6 Menú y accesos: Herramientas → una sola entrada "Multichat…" (abre Configurar). Dentro de Configurar: botones "Asistente de inicio", "Buscar actualizaciones" y "Añadir fuentes a la escena" (reusa `addToCurrentScene` del asistente; añade chat + destacado + voz si no están). Rueda del dock: Configurar, Mensajes de prueba, Mostrar hora, Tamaño del texto, Asistente, Buscar actualizaciones. Quitar el botón "Volver a detectar" de la cabecera (queda en la rueda).
- T7 Asistente: preselecciona la plataforma desde `streamingService()` (mapa servicio→plataforma; desconocido → Twitch); paso de fuentes con un solo botón "Añadir a la escena" que agrega chat + destacado + voz (casillas para quitar alguna); al terminar, muestra el dock (`obs_frontend_*` dock visible) y lo dice. Objetivo: ≤5 decisiones.
- T7b Modo de aviso sonoro en la voz (añadido por el usuario el 2026-11-08): la fuente "Multichat (voz)" gana una lista `mode` = `read` (por defecto, lo de hoy) | `sound` (un aviso corto por mensaje, sin leer) | `both` (aviso y luego lectura). Los filtros por plataforma/evento valen para los tres modos. Aviso integrado sintetizado en C++ (dos tonos senoidales cortos con envolvente, ~250 ms, −12 dBFS) en PCM a la frecuencia del motor, sin recurso empaquetado; función pura `tts::notificationChime(sampleRate)` con pruebas. Sonido propio opcional `sound_file` (WAV PCM 16 bits mono/estéreo, máx. 10 s, parser acotado); si la frecuencia difiere, remuestreo lineal (función pura con pruebas); si no se puede leer, aviso en el log y se usa el integrado. Antiinundación `sound_cooldown` (ms, por defecto 1500, mín. 0): como mucho un aviso por ventana; en `both` solo limita el aviso, la lectura se encola como hoy (función pura con pruebas). Sale por la misma salida PCM que la voz (mezclador, silencio y monitor aplican) y respeta la cancelación: un mensaje borrado que aún no sonó no suena. Asistente y README lo mencionan en una línea.
- T8 README, compilación, pruebas e instalación local.

## Out of scope
- Fase B (refactors internos) y Fase C (fusionar reloj, pruebas de manager/JS, instaladores).
- Nuevas funciones.

## Constraints
- Pruebas: `cmake -S tests -B build_tests_brew -DCMAKE_PREFIX_PATH=$(brew --prefix qt) && cmake --build build_tests_brew -j8 && ctest --test-dir build_tests_brew --output-on-failure`. Base: test-chat 67, test-tts 50, test-overlay 17. RED→GREEN para lógica pura (duración del auto-ocultar, mapa servicio→plataforma, migración de `preview`).
- Compilación macOS sin warnings (`-Werror`). Sin módulos Qt nuevos. Sin renombrar claves de ajustes de OBS.
- Una prueba comprueba que ninguna clave de locale quedó huérfana ni sin traducir en ambos idiomas.

## Delivery
- Rama `feat/simplify-ux`, apilada sobre `feat/quality` (2432905). Sin push.
- Forecast: 800–1100 líneas de autor.
- Native review: RDD apagado globalmente.

## Tasks
- [x] T1 Voz activada por defecto. Route: delegated (writer).
  - Cambio: `src/tts/voice-source.cpp` `obs_data_set_default_bool("enabled", true)`; `Voice.Enabled.Help` (es/en) explica que se silencia desde el mezclador; README paso 2 de la voz.
  - Compatibilidad: solo cambia el valor por defecto; una fuente guardada con `enabled=false` conserva su valor (OBS guarda el valor de usuario, no el default).
  - Checks: sin lógica pura (API `obs_data`): excepción RED/GREEN, lectura estructural del diff. Pruebas base: ver T2.
  - Commit: b152488.
- [x] T2 Destacado: auto-ocultar en C++ y dock sincronizado. Route: delegated (writer).
  - Lógica pura en `src/chat/message.{hpp,cpp}`: `FeaturedHidePolicy`, `featuredTextLength()` (emote = 2, cuenta `system`), `featuredDurationMs(len, policy)` (−1 = nunca) y `featuredDurationMs(len, policies)` (la más corta activada).
  - `ChatManager`: `featuredTimer_` (single shot) → `clearFeatured()` al vencer; `setFeaturedAutoHide(sourceId, policy)` / `removeFeaturedAutoHide(sourceId)` (encolados al hilo principal); `armFeaturedTimer()` cuenta desde `featuredAt_`, así cambiar la política acorta o alarga lo que queda. `reshowLast()` sigue con `lastFeatured_`.
  - `overlay-source.cpp`: cada fuente de destacado informa su política en create/update (`readSettings`) y la retira en `ov_destroy`; el payload ya no manda `now`.
  - `featured.html`: sin temporizador propio; oculta al recibir `message: null`. Regla documentada en README (la más corta de las activadas; ninguna activada = no se oculta).
  - Checks: RED observado (`error: use of undeclared identifier 'featuredTextLength'`), GREEN `test-chat featuredDurationFollowsTheLength` PASS; `ctest` 3/3 (test-chat 68, test-tts 50, test-overlay 17); `cmake --build --preset macos` BUILD SUCCEEDED, 0 warnings.
  - Manual pendiente: destacar, esperar el vencimiento y ver que el dock deja de resaltar; atajo "volver a mostrar" tras vencer.
- [x] T3 Vocabulario unificado. Route: delegated (writer).
  - Regla: *Ocultar* = quitar de Multichat (`Dock.Menu.HideMessage/HideUser`, `Settings.Filters.*` sin cambios); *Silenciar canal* = chip (`Dock.ChipHint`); *Pausar* = desconectar (`Settings.Row.EnabledTip`, `Settings.ListHelp`); *Filtros* = reglas; "canal" para lo que se añade (`Settings.Chats`, `Settings.Tab.Chats`, `Settings.NoChats`, `Settings.Validate.Duplicate`, `Dock.Empty`, `Dock.Configure`, `Source.Chats`, `Wizard.Chat.Later`, `Wizard.Done.TipSettings`, `Settings.Mode.AutoHelp`); "en directo" siempre (`Live.Yes`, `Live.No`, `Dock.LiveOn`, `Dock.Refresh`, `Viewers.Preview`); "en pantalla" en vez de "overlay" (`Settings.Demo`, README). `Source.Fade` → "Desaparecen después de", `Group.Animation` → "Animación y duración", `Featured.AutoHide` → "Se quita solo", `Text.Weight.900` → "Máxima", `Dock.Settings` → "Configurar Multichat". Claves sin cambiar: nombres (`Source.Name`), `Status.*`, `Error.*` (ya uniformes en minúscula y sin punto final), `Update.*`, `Menu.*`/`Update.MenuItem` (los toca T6), `Wizard.*` salvo las dos citadas (T7 reescribe el asistente).
  - Huérfanas eliminadas en ambos .ini: `Source.Opacity`, `Settings.Validate.Found`.
  - Prueba nueva `localeFilesHaveTheSameKeysAndNoOrphans` (test-chat): mismo conjunto de claves en los dos .ini, ninguna vacía y cada clave usada en `src/` (literal completo, prefijo dinámico `"Status."` o raíz numérica `"Overlay.Sample"`); `tests/CMakeLists.txt` define `MULTICHAT_SRC_DIR`. RED observado: `locale key is not used by the plugin: Source.Opacity`; GREEN tras quitar las huérfanas.
  - Checks: `ctest` 3/3 (test-chat 69, test-tts 50, test-overlay 17).
  - Commit anterior (T2): 62f4eaf.
- [x] T4 Mensajes de prueba globales con migración. Route: delegated (writer).
  - `ChatManager`: `testMessages()` / `setTestMessages(on)` (persistido como `testMessages` en config.json, señal `testMessagesChanged`), `configure(chats, mode)` ya no recibe `demo`; `adoptLegacyPreview(bool)` (encolado) aplica la migración. Lógica pura en `platforms.{hpp,cpp}`: `testMessagesEnabled(config)` (ausente = apagado) y `adoptLegacyPreview(config, legacyPreview)`.
  - Fuentes: `overlay-source.cpp` manda `sample` en la config de cada página (atómico alimentado por la señal) y las de espectadores lo leen directo; `preview` desaparece de `readConfig` y de las Propiedades (chat, espectadores, destacado); `index.html` y `featured.html` leen `config.sample`.
  - UI: rueda del dock → menú (Configurar…, Mensajes de prueba ✓); Configurar y el asistente usan el mismo interruptor y se sincronizan por la señal.
  - Migración (decisión): el default de `preview` era `true`, así que "cualquier fuente con preview=true" sería todo el mundo y encendería el chat de prueba del dock (y la voz) a todos al actualizar. Se migra solo si alguna fuente tiene `preview` **guardado por el usuario** (`obs_data_has_user_value`) en `true` y nadie decidió aún (`testMessages` ausente). El valor por defecto `preview=true` sigue en la tabla (`overlay-presets.cpp`, comentado como legado) para no tocar `legacyKeysKeepTheirDefaults`.
  - Locales: fuera `Source.Preview`, `Viewers.Preview`, `Featured.Preview`; nuevas `Dock.Menu.Configure`, `Dock.Menu.TestMessages`.
  - Checks: RED observado (`use of undeclared identifier 'testMessagesEnabled'`), GREEN `testMessagesSwitchAndLegacyPreview` PASS; `test-chat` 70, `test-tts` 50, `test-overlay` 17; `cmake --build --preset macos` BUILD SUCCEEDED, 0 warnings.
  - Manual pendiente: activar desde la rueda y ver que Configurar y las fuentes siguen; abrir una escena vieja con `preview` activado a mano y comprobar que el interruptor queda encendido una sola vez.
  - Commit anterior (T3): 740eb13.
- [x] T5 Propiedades con "Mostrar opciones avanzadas". Route: delegated (writer).
  - Nuevo ajuste `advanced` (bool, por defecto `false`, en `addText` de `overlay-presets.cpp`, común a las 4 fuentes). Orden: Preajuste → Diseño → Plataformas → (reloj de la cinta en el chat) → casilla `Source.Advanced` → Texto → Fondo → Animación (destacado) → Avanzado. `showAdvancedGroups()` oculta `group_text/background/animation/advanced` salvo con la casilla; se llama desde `ov_layout_changed`, `fe_changed` y `box_changed`; `advanced` entra en `changesVisibility`.
  - Movidos a Avanzado sin renombrar claves: chat `fade`, `max` (visibles solo en vertical, antes era el grupo entero), `spacing`, `hspacing`, `pad_y/pad_x`, `panel` + `panel_*`; espectadores/reloj/destacado `pad_y/pad_x`. El grupo "Animación" del chat desaparece (quedaba vacío); el "Diseño" del reloj también (solo tenía el relleno).
  - Locales: `Source.Advanced`, `Source.Advanced.Help`. README: sección Aspecto y la fila de la tabla.
  - Checks: RED observado en `legacyKeysKeepTheirDefaults` (`chat: advanced` ausente tras añadir la clave al mapa de legado), GREEN tras el default; `test-overlay` 17, `test-chat` 70; `cmake --build --preset macos` BUILD SUCCEEDED, 0 warnings.
  - Manual pendiente: abrir Propiedades de las 4 fuentes, ver solo lo esencial, activar la casilla y ver el resto; en horizontal `fade`/`max` ocultos dentro de Avanzado.
  - Commit anterior (T4): 51f22b3.
- [x] T6 Un menú, botones en Configurar, rueda del dock. Route: delegated (writer).
  - Herramientas: una sola entrada `Menu.Settings` = "Multichat…" (abre Configurar); fuera `Menu.Wizard` y `Update.MenuItem`.
  - Configurar: en lugar del texto `Settings.OverlayHelp`, botones "Añadir fuentes a la escena" (chat + destacado + voz, solo los que falten, con el resultado debajo), "Asistente de inicio" y "Buscar actualizaciones".
  - `addToCurrentScene` y compañía pasan del asistente a `ui-common` (`ui::editedSceneName`, `ui::addToCurrentScene`, `ui::addMultichatSources(chat, featured, voice)`); el asistente los reutiliza.
  - Rueda del dock: menú reconstruido al abrirse (`fillGearMenu`): Configurar…, Mensajes de prueba ✓, Mostrar hora ✓, Tamaño del texto ▸ (más grande / más pequeño), Volver a detectar…, Asistente de inicio, Buscar actualizaciones. El botón "Volver a detectar" sale de la cabecera (`data/icons/refresh.svg` queda sin uso en código; se deja por si vuelve).
  - Locales: nuevas `Dock.Menu.TextSize/Wizard/CheckUpdates`, `Settings.AddSources(.Help)/Wizard/CheckUpdates`; textos del asistente y README apuntan a "Herramientas → Multichat…" y a la rueda.
  - Checks: sin lógica pura nueva (excepción RED/GREEN: solo UI/menús); `localeFilesHaveTheSameKeysAndNoOrphans` PASS tras quitar las 3 claves; `cmake --build --preset macos` BUILD SUCCEEDED, 0 warnings.
  - Manual pendiente: Herramientas muestra solo "Multichat…"; los tres botones de Configurar funcionan; la rueda muestra las 7 entradas con las marcas correctas.
  - Commit anterior (T5): 2e8734c.
- [x] T7 Asistente: plataforma detectada, un botón, muestra el dock. Route: delegated (writer).
  - Lógica pura `chat::platformForService(service)` (`platforms.{hpp,cpp}`): busca youtube/kick/tiktok/starvios/twitch en el nombre del servicio sin mayúsculas; lo demás (Restream, Custom, vacío) → twitch. `streamingService()` sale de `plugin-main` a `ui::streamingServiceName()` (ui-common) para que el asistente lo use.
  - `ChannelInput::setPlatform(id)`; el asistente preselecciona con el servicio de OBS. Paso de fuentes: un botón principal "Añadir a la escena «X»" que pone chat + destacado + voz (casillas marcadas por defecto para quitar alguna; ayuda de la voz menciona el modo de T7b). Al terminar (`Accepted`) `show_multichat_dock()`: la API frontend de OBS 33 no tiene "mostrar dock" (solo `add_dock_by_id`/`remove_dock`), así que se sube por los padres del widget hasta el `QDockWidget` de OBS y se hace `setVisible(true)` + `raise()`.
  - Decisiones antes (12): Siguiente en bienvenida, elegir plataforma, escribir canal, Añadir, casilla mensajes de prueba, Siguiente, casilla chat, casilla destacado, Añadir a la escena, Siguiente, Terminar, buscar el dock en Docks. Después (5): Siguiente, escribir canal + Añadir, Añadir a la escena, Siguiente, Terminar (la plataforma viene elegida, las fuentes vienen marcadas, el dock se abre solo; mensajes de prueba y desmarcar fuentes son opcionales y no obligan a decidir).
  - Locales: `Wizard.Chat.Text`, `Wizard.Sources.Text`, `Wizard.Done.TipDock` reescritas; nueva `Wizard.Sources.VoiceHelp`. README: párrafo del asistente.
  - Checks: RED observado (`use of undeclared identifier 'platformForService'`), GREEN `platformFromStreamingService` PASS; `test-chat` 71; `cmake --build --preset macos` BUILD SUCCEEDED, 0 warnings.
  - Manual pendiente: con el servicio de OBS en YouTube, el asistente arranca en YouTube; el botón añade las tres fuentes; al Terminar aparece el dock aunque estuviera cerrado.
  - Commit anterior (T6): 5f4a9a0.
- [x] T7b Voz: modo de aviso sonoro (`read` | `sound` | `both`), WAV propio y antiinundación. Route: delegated (writer).
  - Módulo puro `src/tts/notification-sound.{hpp,cpp}`: `notificationChime(rate)` (A5 120 ms + E6 130 ms, ataque 8 ms, caída exponencial τ=60 ms, salida 20 ms, pico 0,25 = −12 dBFS, mono), `parseWav(bytes, maxSeconds)` (RIFF/WAVE PCM 16 bits mono/estéreo, chunks acotados al tamaño del archivo, salta chunks extra, rechaza 8/24 bits, >2 canales, truncados, >10 s), `resamplePcm(in, rate)` (lineal), `chimeAllowed(now, last, cooldownMs)`; `kChimeSampleRate` = 48000.
  - `VoiceEngine::enqueueSound(pcm, id)`: el sonido entra en la misma cola que los textos (orden, ritmo real, `cancel(ids)` lo tira si el mensaje se borra) sin crear el sintetizador.
  - Fuente de voz: ajustes `mode` ("read" por defecto), `sound_file` (ruta, filtro WAV), `sound_cooldown` (ms, 1500, 0–60000); `voice_mode_changed` muestra las opciones de sonido solo con sonido y las de voz solo con lectura. El WAV se lee fuera del candado y solo si cambia la ruta; si falla, aviso en el log y aviso integrado. En `onMessage` la puerta es `speechTextForMessage` no vacío (filtros de plataforma/palabras/recuperados valen para los tres modos); el antiinundación solo limita el sonido.
  - Locales: `Voice.Mode(.read/.sound/.both/.Help)`, `Voice.SoundFile(.Help)`, `Voice.SoundFilter`, `Voice.SoundCooldown(.Help)`. README: viñeta en "Leer el chat en voz alta". CMake: nuevo .cpp/.hpp en el plugin y en test-tts.
  - Checks: RED observado (CMake `Cannot find source file: notification-sound.cpp` y fallo de enlace), GREEN: 6 pruebas nuevas PASS (`test-tts` 56); `ctest` 3/3 (71/56/17); `cmake --build --preset macos` BUILD SUCCEEDED, 0 warnings (se corrigió un `-Wmissing-field-initializers` en `enqueue`).
  - Manual pendiente: ver lista al final del documento.
  - Commit anterior (T7): eb0805d.
- [ ] T8 README, compilación e instalación local. Route: delegated (writer).

## Progress
- Rama creada desde `feat/quality` (2432905). Bloque 6 probado por el usuario: funciona.
- Pruebas base antes de T1: `ctest` 3/3 passed (test-chat, test-tts, test-overlay).
- T1 hecho (commit: ver T2, se registra en la tarea siguiente).

## Next step
T1–T8 con un writer delegado.
