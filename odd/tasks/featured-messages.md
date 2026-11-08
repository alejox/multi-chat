# Feature: featured-messages

## Objective
Que el streamer haga clic en un mensaje del dock "Multichat" y ese mensaje aparezca destacado y grande en pantalla, mediante una fuente nueva "Multichat (destacado)", como el "featured chat" de Social Stream Ninja pero nativo de OBS.

## Problem and why
Hoy el dock solo muestra el chat; no hay forma de poner un comentario concreto en escena. Es una función estándar en producción en vivo (Social Stream Ninja, StreamYard, Restream). Dentro del plugin, el dock y la fuente comparten proceso, así que no hace falta transporte de red como el WebRTC de SSN.

## Scope
- Arreglar la inversión de orden de locks en `src/overlay-source.cpp` (`ov_update` mantiene `sourcesMutex` mientras llama a `fitAllScenes`/`obs_source_update`; `ov_destroy` también lo pide).
- Estado "destacado" en C++ (`ChatManager`): mensaje actual, destacar, quitar, señales. Se reenvía al recargar la página.
- Fuente nueva `multichat_featured` (`Kind::Featured`) con `data/overlay/featured.html`: avatar si hay, nombre con color, logo de plataforma, insignias, texto plano + emotes, monto de donación; entrada/salida con fundido o deslizamiento; ocultar automáticamente (base + ms por carácter, desactivable); tamaño, posición, estilos de fondo existentes.
- Dock: clic en un mensaje lo destaca (reemplaza el actual); clic en el destacado o Esc lo quita; el destacado se resalta en el dock.
- Hotkeys de OBS: "Quitar mensaje destacado" y "Volver a mostrar el último destacado".
- Textos en `data/locale/en-US.ini` y `es-ES.ini`; README.
- Compilar e instalar en el OBS local de macOS para probar.

## Out of scope
- Cola (Ctrl+Clic), rotación, auto-destacar donaciones, historial, ajuste de texto al tamaño, API obs-websocket, sonido.
- Quitar automáticamente mensajes borrados por la plataforma (los conectores no exponen eventos de borrado hoy).
- Subir versión, release, push, PR, merge.

## Constraints
- El texto del chat se trata siempre como texto plano en la página (`textContent`, nunca `innerHTML` con datos del chat).
- El estado vive en C++: la página no guarda nada propio.
- Sin warnings en la compilación de macOS (`cmake --preset macos && cmake --build --preset macos`).
- Las pruebas QtTest no configuran en macOS (falta Qt6Test en `.deps`); la verificación local es la compilación y la prueba manual en OBS.

## Delivery
- Forecast: unas 600–800 líneas de autor.
- Strategy: `ask-on-risk`; por ahora rama `feat/featured-messages`, commits por tarea, sin push.
- Native review: RDD apagado globalmente (`gentle-ai review mode status` → off by global).

## Tasks
- [x] T1 Arreglar el orden de locks en `overlay-source.cpp`. Route: delegated (writer junto con T2–T4; misma zona de código).
  - Accept: `ov_update` no mantiene `sourcesMutex` mientras llama a funciones de OBS que toman locks de escena.
  - Check: compilación macOS sin warnings.
  - Evidence: `sourcesMutex` solo cubre `sources` y `lastSources`; los recorridos (`sendAll`, heartbeat, `overlay_set_streaming`, espectadores) toman una referencia de cada fuente con `obs_source_get_ref` bajo el candado y llaman a OBS fuera; `config`/`sized` van con un candado por fuente que nunca se mantiene durante llamadas a OBS. `cmake --build --preset macos`: BUILD SUCCEEDED, 0 warnings. Commit: `4f52a7f`.
- [x] T2 Estado destacado en `ChatManager` + fuente `multichat_featured` + `featured.html` + textos. Route: delegated (writer; 4+ archivos no triviales).
  - Accept: la fuente aparece en OBS, muestra el mensaje enviado y lo reenvía tras recargar.
  - Check: compilación macOS sin warnings; prueba manual.
  - Evidence: `ChatManager::feature/clearFeatured/reshowLast` + señal `featuredChanged`; limpiar el chat quita el destacado y olvida el último. Fuente `multichat_featured` (`Kind::Featured`) con `featured.html`; el estado (`message`, `at`, `now`) se manda al crear la fuente, al cambiar y en el latido de 3 s; la página oculta según `at`, así que recargar no alarga el tiempo. Texto solo con `textContent`; emotes/avatar solo http/https. `cmake --build --preset macos`: BUILD SUCCEEDED, 0 warnings; `node --check` del script: OK. Prueba manual en OBS: pendiente (la hace el usuario). Commit: `32b29aa`.
- [x] T3 Dock: clic para destacar/quitar, Esc, resaltado; hotkeys de OBS. Route: delegated (writer).
  - Accept: clic destaca, segundo clic o Esc quita, hotkeys registradas en Ajustes → Atajos.
  - Check: compilación macOS sin warnings; prueba manual.
  - Evidence: cada bloque del dock guarda el id del mensaje (`QTextBlockUserData`); el clic (sin arrastrar, sin selección, fuera de enlaces y dentro del bloque) destaca o quita; Esc en el dock quita; el resaltado cambia solo los bloques afectados (no mueve el scroll). Atajos `multichat.clear_featured` y `multichat.reshow_featured` con `obs_hotkey_register_frontend`; los callbacks pasan al hilo de Qt con `QueuedConnection`; las teclas se guardan en `hotkeys.json` (carpeta de config del plugin) al guardar OBS y al salir, nunca al descargar (OBS ya liberó los atajos: `obs_shutdown` llama a `stop_hotkeys` antes de `free_module`). `cmake --build --preset macos`: BUILD SUCCEEDED, 0 warnings. Prueba manual: pendiente. Commit: `cc5d6d1`.
- [x] T4 README y compilación + instalación local en macOS. Route: delegated (writer).
  - Accept: plugin instalado en `~/Library/Application Support/obs-studio/plugins/obs-multichat.plugin`.
  - Check: OBS carga el plugin y aparece la fuente "Multichat (destacado)".
  - Evidence: README con fila en Funciones y sección "Destacar un mensaje". `cmake --preset macos && cmake --build --preset macos`: BUILD SUCCEEDED, 0 warnings. Instalado con `ditto` en `~/Library/Application Support/obs-studio/plugins/obs-multichat.plugin` (reemplaza la 2.1.1 del 5 nov; copia de seguridad en el scratchpad de la sesión); `codesign -v` OK; el binario contiene `multichat_featured` y los ids de los atajos. OBS no estaba abierto. Carga en OBS y prueba manual: pendiente (usuario). Commit: este commit (`docs(featured): …`).

## Progress
- Rama `feat/featured-messages` creada desde `main` (8edb0e5).

- T1–T4 hechas en `feat/featured-messages` (4 commits). Sin pruebas automáticas: QtTest no configura en macOS (falta Qt6Test en `.deps`); no se añadieron tests (fuera de las superficies permitidas).

## Next step
Prueba manual en OBS (lista en el informe del writer); luego decidir push/PR.
