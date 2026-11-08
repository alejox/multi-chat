# Feature: final-review

## Objective
Corregir los defectos confirmados por la revisión final (dos revisores independientes sobre `main..feat/phase-c`), con una prueba por cada corrección donde sea posible.

## Findings to fix
1. **Alta — voces existentes empiezan a hablar al actualizar** (`src/tts/voice-source.cpp:259`). OBS solo guarda valores que el usuario tocó (`libobs/obs-data.c:578`), así que una fuente de voz creada antes y nunca activada no tiene `enabled` guardado y con el nuevo default `true` lee el chat en directo. Corrección: versionar la fuente con `obs_source_info.version` (OBS 33 lo soporta: `obs-module.c:1122-1126` registra `<id>_v<N>`). `multichat_voice` (versión 0) conserva `enabled=false` por defecto y se marca `OBS_SOURCE_DEPRECATED | OBS_SOURCE_CAP_OBSOLETE`; `multichat_voice` versión 2 (id interno `multichat_voice_v2`) usa `enabled=true`. Las escenas viejas cargan la versión 0 tal como estaban; "Añadir fuente", el asistente y "Añadir fuentes a la escena" crean la nueva. Comprobar en libobs cómo resuelve el id guardado (`versioned_id`) al cargar.
2. **Media-alta — Configurar borra canales que añadió el asistente** (`src/ui/settings-dialog.cpp:298-305`, `:144`, `:173`). Resincronizar `entries_` con `ChatManager::chats()` antes de cada mutación y al recibir cambios de estado.
3. **Media — vaciar dos veces pierde el primer vaciado** (`src/chat-history.cpp:24-27`; también `FeaturedState::stashLast`). Si ya hay algo para deshacer, acumular (lo nuevo después de lo viejo, con tope `kHistoryLimit`).
4. **Baja — ban/timeout no corta la voz en cola en dos casos** y **la cancelación ignora canal/plataforma** (`src/chat-history.cpp:41-56`, `voice-source.cpp onDeleted`). Guardar en la cola de voz la identidad del mensaje (source, platform, id, authorId, author) y cancelar con `chat::deletionMatches`; quitar la fusión del `id` suelto.
5. **Baja — copia del sonido por mensaje** (`voice-source.cpp onMessage` → `VoiceEngine::enqueueSound`). Sonido como `std::shared_ptr<const ...>` y, en modo "sonido y voz", sonido y texto en un mismo elemento de la cola para que no se separen al descartar.
6. **Baja — ocultar usuario desde el dock resucita un usuario recién borrado en Configurar** (`settings-dialog.cpp:243-256`). `filtersChanged` lleva solo el autor nuevo; el diálogo lo añade si no está y no reescribe el texto si nada cambió.
7. **Baja — botón Añadir atascado en "Comprobando…" al cambiar de plataforma** (`src/ui/channel-input.cpp:142-145`, `:226-248`). El cambio de plataforma hace el mismo reinicio que el cambio de texto.
8. **Baja — instalación de Windows que tarda más de 180 s se informa como fallida** (`src/updater/install-win.cpp runElevated`). Tratar `WAIT_TIMEOUT` aparte con su propio mensaje localizado.

## Deferred (documented, not fixed here)
- **Instalación elevada en Windows desde `%TEMP%`** (ya existía en `main`): el zip se verifica, pero el script y la DLL extraída quedan en una carpeta escribible por el usuario entre la verificación y UAC. Requiere rediseñar el script elevado para que verifique y extraiga a una carpeta solo de administradores; no se puede probar sin Windows. Queda como issue prioritario antes del release.

## Constraints
- Pruebas: `cmake -S tests -B build_tests_brew -DCMAKE_PREFIX_PATH=$(brew --prefix qt) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build_tests_brew -j8 && ctest --test-dir build_tests_brew --output-on-failure`. Base: 6/6 suites (test-chat 71, test-tts 56, test-overlay 20, test-box-measure 9, test-manager 30, overlay-js 6).
- RED→GREEN en 3, 4, 5 (lógica pura o con `ChatManager::Environment`) y en lo que se pueda de 1 (al menos los defaults por versión). Compilación macOS sin warnings. Sin push.

## Delivery
- Rama `fix/final-review`, apilada sobre `feat/phase-c` (374083a).

## Tasks
- [x] T1 Voz versionada (hallazgo 1). Route: delegated (writer). Base: 374083a.
  - libobs: `obs-module.c:1122-1129` registra `<id>_v<N>` y guarda `unversioned_id`; `obs.c:2264-2293` carga con `versioned_id` guardado (o `id` si falta) vía `obs_source_create_set_last_ver`; `obs-source.c:52-60` `get_source_info` busca por id exacto, así que `obs_source_create("multichat_voice")` crea la versión 0: la nueva se crea con `obs_get_latest_input_type_id` (`obs.c:1745`). `obs.c:2457,2485` guarda `id` sin versión y `versioned_id`.
  - Cambio: `voice-source.hpp` (`kVoiceSourceId`, `kVoiceSourceVersion = 2`, `voiceEnabledByDefault`), registro doble en `voice-source.cpp` (v0 `DEPRECATED|CAP_OBSOLETE`, `enabled=false`; v2 `enabled=true`), `ui-common.cpp` compara por id sin versión (no duplica una voz vieja en la escena) y crea la última versión; el asistente usa la constante.
  - RED: `test-tts` no compila (`voiceEnabledByDefault`/`kVoiceSourceVersion` no declarados). GREEN: test-tts 57/57, 6/6 suites; compilación macOS BUILD SUCCEEDED sin warnings.
- [ ] T2 Historial/deshacer y cola de voz (hallazgos 3, 4, 5). Route: delegated (writer).
- [ ] T3 Diálogo de configuración y entrada de canal (hallazgos 2, 6, 7) + timeout del instalador (8). Route: delegated (writer).
- [ ] T4 Compilación, pruebas, instalación, README (nota de la fuente de voz versionada y del pendiente de Windows). Route: delegated (writer).
