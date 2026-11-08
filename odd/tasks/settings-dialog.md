# Feature: settings-dialog

## Objective
Que "Herramientas → Multichat: configurar chats" guíe al usuario como un producto premium: no deja añadir canales inexistentes sin avisar, permite pausar y ordenar chats sin borrarlos, y la primera vez lleva de la mano hasta tener el chat en escena.

## Problem and why
Auditoría (snapshot de `main`, líneas pueden haber cambiado con los bloques 2–4):
- El canal se añade aunque la vista previa diga "NotFound" (`settings-dialog.cpp:190` frente a `addEntry` en `:199`).
- No se puede desactivar un chat sin borrarlo, ni reordenar.
- "Demo" se detecta con el heurístico `text.contains(':')` (`:161`).
- Cada fila mezcla estado, emoji y detalle en un solo `QLabel` (`:264-271`).
- No hay primera ejecución guiada: el usuario nuevo tiene que descubrir el menú, añadir el canal y crear la fuente a mano.

## Scope
- Validación al añadir: mientras se resuelve el perfil, indicador de carga; si el canal no existe, aviso claro en línea y el botón Añadir pide confirmación ("Añadir igualmente") en lugar de añadir en silencio. Errores de formato con mensaje por plataforma.
- Detección explícita de la entrada demo (sin `contains(':')`).
- Filas: avatar, nombre, plataforma, estado (conectado / en vivo / error) en elementos separados; interruptor activado/desactivado por fila (un chat desactivado se desconecta pero se conserva en la configuración); botón quitar con nombre accesible.
- Reordenar por arrastre; el orden se guarda y se respeta en los chips del dock.
- Persistencia compatible: `config.json` existente se lee igual; las entradas sin `enabled` cuentan como activadas.
- Asistente de primera ejecución (solo si no hay canales configurados y no se ha mostrado antes): 1) bienvenida, 2) añadir el primer canal (con la misma validación), 3) añadir a la escena actual la fuente "Multichat" (y opcionalmente "Multichat (destacado)") con un clic, 4) listo con atajos útiles. Se puede saltar y volver a abrir desde el menú Herramientas.
- Textos en `en-US.ini` y `es-ES.ini`; README.
- Pruebas para la lógica pura (parseo/validación de entradas, migración de `enabled`, orden).

## Out of scope
- Detectar el servicio de emisión configurado en OBS para rellenar el canal (se valora después).
- Cambios en conectores de plataformas.

## Constraints
- Pruebas: `cmake -S tests -B build_tests_brew -DCMAKE_PREFIX_PATH=$(brew --prefix qt) && cmake --build build_tests_brew -j8 && ctest --test-dir build_tests_brew --output-on-failure`. Base: test-chat 57, test-tts OK, test-overlay 17.
- Compilación macOS sin warnings (`-Werror`).
- No enlazar módulos Qt nuevos (CI Linux solo `qt6-base-dev`).
- Añadir una fuente a la escena usa la API de OBS en el hilo de UI y nunca duplica una fuente ya presente en esa escena.

## Delivery
- Rama `feat/settings-dialog`, apilada sobre `feat/source-properties` (684777a). Sin push.
- Forecast: 700–1000 líneas de autor.
- Native review: RDD apagado globalmente.

## Tasks
- [x] T1 Modelo: `enabled` y orden en la configuración, migración compatible, demo explícita, con pruebas. Route: delegated (writer).
  - Formato: `chats` sigue siendo la lista de textos en el orden del usuario; los pausados van en `disabled` (se omite si está vacío). También se leen objetos `{entry, enabled}`; sin `enabled` cuenta como activado. `save()` conserva las claves que no conoce.
  - Errores de entrada como `chat::EntryError` + clave `Entry.Error.*` (traducidos en la UI y en ChatManager); `chat::entryFromInput` sustituye a `contains(':')`; `chat::isDemo`; `chat::planSources` decide fuentes y orden (demo al final).
  - RED: test-chat no compilaba (20 errores, `EntryError` sin declarar). GREEN: test-chat 61, test-tts 48, test-overlay 17. Compatibilidad: prueba `chatConfigCompat` con un `config.json` del formato anterior (se lee igual y se reescribe idéntico). macOS: BUILD SUCCEEDED, 0 errores, 0 warnings.
- [ ] T2 Validación al añadir (carga, no existe, formato) con confirmación. Route: delegated (writer).
- [ ] T3 Filas nuevas: interruptor, estado separado, arrastrar para ordenar; orden en los chips del dock. Route: delegated (writer).
- [ ] T4 Asistente de primera ejecución con inserción de fuentes en la escena. Route: delegated (writer).
- [ ] T5 README, compilación e instalación local en macOS. Route: delegated (writer).

## Progress
- Rama creada desde `feat/source-properties` (684777a). Bloque 4 probado por el usuario en OBS: funciona.

## Next step
T1–T5 con un writer delegado.
