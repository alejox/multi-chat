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
- [ ] T3 Vocabulario unificado. Route: delegated (writer).
- [ ] T4 Mensajes de prueba globales con migración. Route: delegated (writer).
- [ ] T5 Propiedades con "Mostrar opciones avanzadas". Route: delegated (writer).
- [ ] T6 Un menú, botones en Configurar, rueda del dock. Route: delegated (writer).
- [ ] T7 Asistente: plataforma detectada, un botón, muestra el dock. Route: delegated (writer).
- [ ] T7b Voz: modo de aviso sonoro (`read` | `sound` | `both`), WAV propio y antiinundación. Route: delegated (writer).
- [ ] T8 README, compilación e instalación local. Route: delegated (writer).

## Progress
- Rama creada desde `feat/quality` (2432905). Bloque 6 probado por el usuario: funciona.
- Pruebas base antes de T1: `ctest` 3/3 passed (test-chat, test-tts, test-overlay).
- T1 hecho (commit: ver T2, se registra en la tarea siguiente).

## Next step
T1–T8 con un writer delegado.
