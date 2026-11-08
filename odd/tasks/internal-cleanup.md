# Feature: internal-cleanup (Fase B)

## Objective
Dejar el código listo para mantenerlo en público: sin duplicados, archivos con una sola responsabilidad, `ChatManager` probado sin OBS y configuración guardada de forma segura. **Sin cambios de comportamiento visibles.**

## Problem and why
Revisión de arquitectura (2026-11-08) sobre `feat/quality`, más Fase A:
- `src/overlay-source.cpp` (1541 líneas) mezcla callbacks de OBS, propiedades, config→JSON, medición de cajas de espectadores, ajuste a escenas y el reparto desde el manager.
- `src/ui/chat-dock.cpp` (1163) junta el renderizador `ChatView`, el widget `ChatDock` y las preferencias `dock.json`; `src/ui/settings-dialog.cpp` (850) define `ChannelInput`, `ChatList` y `SettingsDialog` dentro del .cpp.
- 4 envoltorios de `obs_module_text`: `ui::T` (`ui-common.cpp`), `moduleText` (`chat-manager.cpp`), `moduleString` (`overlay-source.cpp`), `text` (`updater.cpp`).
- 4 tablas de plataformas: `kPlatforms` (`tts/voice-source.cpp`), `platformOrder` (`chat-manager.cpp`), `PLATFORMS` (`data/overlay/icons.js`), `ui::platformColor`.
- Páginas: `applyCustomCss` idéntica en las 4; `applyTextStyle` en 3; `safeUrl`, `el`, `nameColor`, `authorColor`, `badgeLabel`, `platformIcon` duplicadas entre `index.html` y `featured.html`. Sin pruebas de JS.
- `ChatManager` hace historial, destacado (máquina de 4 estados con borrados), deshacer, filtros, i18n, persistencia y sondeo; no se puede construir en pruebas (usa `obs_module_config_path`, `obs_module_text`).
- Dos historiales: dock con tope 300 (`messages_`) y manager con tope 200.
- `config.json` sin `version` y guardado no atómico (`QFile` trunca); `dock.json` falla en silencio.
- `hexColor` (`overlay-source.cpp`) duplica `overlay::cssColor`.

## Scope
- T1 Red de seguridad: pruebas de caracterización de `ChatManager` (recepción, filtros, borrados, destacado + expiración, deshacer, mensajes de prueba, persistencia) haciendo el manager construible con un `ConfigStore` (ruta inyectable) y un traductor inyectable; las llamadas a OBS quedan detrás de esas dos piezas. Nuevo `tests/test-manager.cpp`.
- T2 Extraer `ChatHistory` (historial, tope, aplicar borrados, deshacer) y `FeaturedState` (actual, último, expiración, interacción con borrados) como clases Qt Core propiedad del manager. Un solo tope de historial compartido por manager y dock (el dock renderiza desde el historial del manager o usa la misma constante; elegir la opción más simple y documentarla).
- T3 `config.json` con `"version": 1` (leer sin versión = 1), guardado atómico con `QSaveFile` para `config.json` y `dock.json`, y registro de errores en ambos.
- T4 `src/i18n.hpp` único (`tr(key)` + traductor) y `src/platforms-registry.hpp` único (id, nombre, orden, color); borrar los 4 envoltorios y las tablas C++ duplicadas. `icons.js` mantiene su tabla (es JS) pero se comprueba con una prueba que coincide con el registro C++ (ids y orden).
- T5 `data/overlay/overlay.js` compartido con las funciones duplicadas; las 4 páginas lo cargan con `<script src>`. Las funciones puras se exportan con guarda `module.exports` y se prueban con `node --test` (`tests/overlay-js.test.mjs` o `.js`), registrado en `tests/CMakeLists.txt` como prueba de ctest solo si se encuentra `node`.
- T6 Partir `overlay-source.cpp` en `src/overlay/` (`source.cpp`, `properties.cpp`, `config.cpp`, `viewers-layout.cpp`, `wiring.cpp` + cabecera interna); quitar `hexColor` en favor de `overlay::cssColor`. Movimientos puros.
- T7 Partir `chat-dock.cpp` (`chat-view.cpp`, `chat-dock.cpp`, `dock-prefs.cpp`) y `settings-dialog.cpp` (`channel-input.cpp`, `chat-list.cpp`, `settings-dialog.cpp`) con sus cabeceras. Movimientos puros.
- T8 Compilación, pruebas, instalación local y README (solo la sección de estructura del código).

## Out of scope
- Cualquier cambio visible para el usuario (textos, opciones, comportamiento).
- Fusionar el reloj en el chat, instaladores del actualizador, medir fuentes en el hilo de UI (Fase C).

## Constraints
- Comportamiento idéntico: las pruebas existentes pasan sin modificar sus expectativas (solo se permite cambiar includes o nombres de símbolos movidos).
- Pruebas: `cmake -S tests -B build_tests_brew -DCMAKE_PREFIX_PATH=$(brew --prefix qt) && cmake --build build_tests_brew -j8 && ctest --test-dir build_tests_brew --output-on-failure`. Base: test-chat 71, test-tts 56, test-overlay 17.
- Compilación macOS sin warnings (`-Werror`). Sin módulos Qt nuevos (`QSaveFile` es Qt Core). Linux/Windows deben seguir compilando: archivos nuevos añadidos a `CMakeLists.txt` sin condiciones de plataforma salvo las que ya existen.
- Editor: si se mueven archivos a `src/overlay/`, `.clangd` ya cubre `src/.*`.

## Delivery
- Rama `refactor/internal-cleanup`, apilada sobre `feat/simplify-ux` (b137ee7). Sin push.
- Forecast: mucho movimiento (~2.500 líneas movidas) y ~600–900 líneas de autor nuevas (pruebas, clases extraídas).
- Native review: RDD apagado globalmente.

## Tasks
- [x] T1 Pruebas de caracterización de `ChatManager` + `ConfigStore`/traductor inyectables. Route: delegated (writer).
  - `ChatManager::Environment` (store, traductor, log, fábrica de conectores, `checkLive`, reloj); `plugin-main.cpp` instala el de OBS con `ChatManager::install()`. Nuevo `src/config-store.*` y `tests/test-manager.cpp` (25 casos, conectores y reloj falsos).
  - Checks: test-chat 71, test-tts 56, test-overlay 17, test-manager 25 (3 corridas seguidas sin fallos); build macOS SUCCEEDED, 0 errores, 0 warnings.
- [x] T2 `ChatHistory` + `FeaturedState`; un solo tope de historial. Route: delegated (writer). Previo: T1 `0f02d58`.
  - `src/chat-history.*` (historial, tope, borrados, copia para deshacer) y `src/featured-state.*` (actual, último, copia para deshacer, políticas y tiempo restante); el temporizador y las señales siguen en `ChatManager`.
  - Tope único: `chat::kHistoryLimit = 300` en `chat/message.hpp`, usado por el manager y por `ChatView`. Se eligió la constante compartida (y no que el dock pinte desde el manager) porque el dock recorta, filtra y reconstruye su propia lista; se eligió 300 (el del dock) para que lo visible no cambie. Efecto: el manager guarda 300 en vez de 200 (ver "Found bugs").
  - Checks: test-chat 71, test-tts 56, test-overlay 17, test-manager 27 (las dos pruebas de tope ahora usan `chat::kHistoryLimit`; +2 de las clases nuevas); build macOS SUCCEEDED, 0 errores, 0 warnings.
- [x] T3 `version` + guardado atómico. Route: delegated (writer). Previo: T2 `78cc928`.
  - `readJsonObject`/`writeJsonObject` (con `QSaveFile`) en `src/config-store.*`; `ConfigStore` escribe `"version": 1` y lee un archivo sin versión como 1. `dock.json` usa las mismas funciones y registra el fallo con `obs_log`.
  - RED observado: `configStoreWritesTheVersion` y `configStoreReadsAMissingVersionAsOne` fallaron con el guardado anterior (28 passed, 2 failed); GREEN tras el cambio.
  - Checks: test-chat 71, test-tts 56, test-overlay 17, test-manager 30; build macOS SUCCEEDED, 0 errores, 0 warnings.
- [x] T4 `i18n.hpp` y registro de plataformas únicos. Route: delegated (writer). Previo: T3 `f511306`.
  - `src/i18n.{hpp,cpp}`: `i18n::tr(key)` y `i18n::translator()` (siempre calificado: dentro de un `QObject` un `tr()` suelto es el de Qt). Borrados `ui::T`, `text` (updater), `moduleString` (overlay) y la lambda de `plugin-main`.
  - `src/platforms-registry.hpp`: id, nombre, color y orden. Lo usan `chat::platformName`, `ui::platformColor`, el orden de `ChatManager::state()`, las casillas de la voz y los `show_*` del overlay. `icons.js` conserva su tabla; `iconsJsMatchesTheRegistry` (test-overlay) compara ids, orden y colores.
  - Sin RED: el registro se escribió antes que sus pruebas; se comprobó que la prueba de `icons.js` falla cambiando un color del registro (mutación revertida).
  - Checks: test-chat 71, test-tts 56, test-overlay 19, test-manager 30; build macOS SUCCEEDED, 0 errores, 0 warnings.
- [ ] T5 `overlay.js` compartido + pruebas con node. Route: delegated (writer).
- [ ] T6 Partir `overlay-source.cpp`. Route: delegated (writer).
- [ ] T7 Partir `chat-dock.cpp` y `settings-dialog.cpp`. Route: delegated (writer).
- [ ] T8 Compilación, pruebas, instalación y README. Route: delegated (writer).

## Progress
- Rama creada desde `feat/simplify-ux` (b137ee7). Fase A instalada; prueba manual del usuario pendiente.

## Found bugs
- Topes distintos (dock 300, manager 200): un mensaje que el dock mostraba más allá de los 200 últimos no se podía destacar (`feature(id)` no lo encontraba) y tras "Deshacer" el dock volvía con 200. Queda resuelto como efecto de T2 (alcance pedido), no por un arreglo aparte.

## Next step
T1–T8 con un writer delegado.
