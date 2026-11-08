# Feature: source-properties

## Objective
Que configurar las fuentes "Multichat" (chat, destacado, espectadores, fecha y hora) sea rápido y se vea premium: propiedades agrupadas, presets de un clic, control completo de tipografía y CSS propio para usuarios avanzados.

## Problem and why
Auditoría: `src/overlay-source.cpp` registra unas 71 llamadas `obs_properties_add*` sin ningún `obs_properties_add_group`; las casillas `show_*` se repiten. La fuente del texto está fija (`"Segoe UI", system-ui` en `data/overlay/index.html:13`), sin color, contorno ni sombra configurables, y `css` del browser source se fuerza a `""`. Además, "Deshacer" al vaciar el chat (bloque 3) no devuelve los mensajes al overlay.

## Scope
- Agrupar propiedades con `obs_properties_add_group` (OBS_GROUP_NORMAL) en cada tipo de fuente: Diseño, Texto, Plataformas e insignias, Fondo, Animación / Ocultar, Avanzado (y Reloj donde aplique). Quitar duplicados.
- **Compatibilidad:** se conservan todas las claves de configuración existentes; las escenas guardadas de usuarios siguen igual. Claves nuevas con valores por defecto que reproducen el aspecto actual.
- Tipografía (chat y destacado; espectadores y reloj si encaja): familia de fuente (`obs_properties_add_font` o lista; la página la aplica con `font-family` y fallback a la pila actual), peso, color del texto, color del nombre (de la plataforma / del usuario / fijo), contorno (grosor + color), sombra (on/off + color + desenfoque), interlineado.
- Presets: selector arriba ("Personalizado", y 5–6 presets propios con identidad clara, p. ej. Limpio, Neón, Burbujas, Minimal en una línea, Alto contraste, Vertical 1080×1920). Aplicar un preset escribe varias claves una sola vez (callback de modificación) y luego vuelve a "Personalizado" si el usuario toca algo; nunca bloquea los controles.
- Avanzado: CSS personalizado (texto multilínea) inyectado en la página como `<style>` con `textContent`; la página nunca evalúa JS de esa caja.
- Deshacer al vaciar: el overlay recibe los mensajes restaurados mediante un evento propio (p. ej. `type: "restore"`), sin pasar por la señal `message` para que la voz no los lea de nuevo.
- Textos en `en-US.ini` y `es-ES.ini` (con `.Help` donde haga falta); README.
- Pruebas para la lógica pura (mapeo de presets a ajustes, saneado de CSS/colores si se extrae).

## Out of scope
- Servidor HTTP/URL para usar el overlay fuera de OBS.
- Galería de temas en la nube.
- Rediseño del diálogo de configurar chats (bloque 5).

## Constraints
- Pruebas: `cmake -S tests -B build_tests_brew -DCMAKE_PREFIX_PATH=$(brew --prefix qt) && cmake --build build_tests_brew -j8 && ctest --test-dir build_tests_brew --output-on-failure`. Base: test-chat 57, test-tts OK.
- Compilación macOS sin warnings (`-Werror`; buscar ` error:` y `BUILD (SUCCEEDED|FAILED)` en el log).
- No enlazar módulos Qt nuevos que el CI de Linux no instala (solo `qt6-base-dev`).
- Datos del chat siguen sin `innerHTML`.

## Delivery
- Rama `feat/source-properties`, apilada sobre `feat/dock-ux` (63c01d1). Sin push.
- Forecast: 700–1000 líneas de autor.
- Native review: RDD apagado globalmente.

## Tasks
- [x] T1 Agrupar propiedades de las cuatro fuentes sin cambiar claves. Route: delegated (writer).
  - Grupos `OBS_GROUP_NORMAL` (`group_layout`, `group_text`, `group_platforms`, `group_background`, `group_animation`, `group_clock`, `group_advanced`); los nombres de grupo no guardan nada en los ajustes. `addPlatforms()` quita la lista `show_*` repetida. En horizontal se ocultan las secciones enteras «Animación y ocultar» (vertical) y «Fecha y hora en la cinta» (solo horizontal).
  - Compatibilidad: claves de propiedades antes/después (extraídas con `rg` de `obs_properties_add_*`): 45 = 45, `diff` vacío. `*_defaults` sin cambios (`git diff` no toca `obs_data_set_default_*`). OBS 33 `obs_properties_get` busca dentro de los grupos y los callbacks reciben las propiedades raíz (libobs/obs-properties.c:296, :806), así que la lógica de visibilidad sigue igual.
  - Checks: build macOS `BUILD SUCCEEDED`, 0 ` error:`, 0 ` warning:`. Sin prueba RED/GREEN: es solo UI de OBS (prueba manual en el checklist de T5).
  - Commit: `895d39a`.
- [x] T2 Tipografía (familia, peso, colores, contorno, sombra, interlineado) en páginas y propiedades. Route: delegated (writer).
  - Familia: lista (no `obs_properties_add_font`, que trae tamaño y estilo propios y chocaría con `font_size`) con «Por defecto» + `QFontDatabase::families()` (sin familias privadas ni las que empiezan por `.`). El tamaño sigue siendo `font_size`. La página recibe `font` = `overlay::cssFontFamily(face)` (cara entre comillas con `"`/`\` escapados, sin `;{}<>` ni caracteres de control, máx. 128 bytes sin cortar UTF-8, + pila por defecto).
  - Claves nuevas (todas con valores por defecto que reproducen el aspecto anterior): `font_family`, `font_weight`, `text_color`, `outline_width`, `outline_color`, `text_shadow` (auto/on/off; auto = lo de antes), `shadow_color` (con alfa), `shadow_blur`; chat y destacado además `name_mode` (user/platform/fixed), `name_color`, `line_height`. Ninguna clave existente cambia: diff de claves de propiedades = solo esas 11 añadidas.
  - Colores OBS `0xAABBGGRR` → `overlay::cssColor` → `rgba(...)` (alfa con enteros, sin depender del separador decimal del locale). El peso aplica al texto del mensaje (los nombres siguen en 800); en espectadores/reloj, a todo. El tamaño medido en C++ usa la fuente y el peso elegidos; el alto de la cinta usa el interlineado.
  - Pruebas nuevas `tests/test-overlay.cpp` (target `test-overlay`, sin libobs; el CI ejecuta `ctest` genérico, así que corre). RED observado: 6 fallos de 8 con stubs vacíos; GREEN: 8/8.
  - Checks: `node --check` de los scripts de las 4 páginas OK; build macOS `BUILD SUCCEEDED`, 0 errores, 0 warnings; ctest 3/3 (test-chat 57, test-tts OK, test-overlay 8).
  - Nota: las funciones JS de tipografía van dentro de cada página (no en `icons.js`, que quedó fuera de la superficie autorizada).
  - Commit: `8b8ad60`.
- [x] T3 Presets con pruebas del mapeo + CSS personalizado. Route: delegated (writer).
  - Los valores por defecto de las cuatro fuentes pasan a una tabla pura (`overlay::defaultSettings`, `src/overlay-presets.cpp`); OBS los registra con `applyDefaults`. `Kind` es ahora `overlay::SourceKind`. Se añadieron como explícitos `clock=false` y `ticker_width=0` (antes implícitos, mismo valor).
  - Compatibilidad: la prueba `legacyKeysKeepTheirDefaults` fija las claves y valores por defecto de antes del bloque 4 para cada tipo (chat 46, espectadores 19, reloj 15, destacado 22) y pasa; diff de claves de propiedades contra el inicio: ninguna quitada.
  - Preajustes (`preset`, arriba en las cuatro fuentes): Limpio, Neón, Burbujas, Minimal en una línea, Alto contraste, Vertical 1080×1920. Cada valor lleva los tipos de fuente a los que aplica. Un callback común (`on_modified`, `obs_property_set_modified_callback2` en todas las propiedades) aplica el preajuste solo si cambia respecto a `preset_applied` (OBS vuelve a llamar los callbacks al abrir/refrescar) y vuelve a «Personalizado» cuando cambia un valor que el preajuste escribió. Solo refresca la ventana si cambia la visibilidad o el preajuste, para no cortar la escritura en el CSS. Decisión: «tocar algo» = cambiar un valor que el preajuste fijó (las claves que no fija, como `max` en la mayoría, no lo sacan del preajuste).
  - CSS personalizado (`custom_css`, multilínea, grupo Avanzado en las cuatro): `overlay::sanitizeCustomCss` (máx. 20 KB sin cortar UTF-8, sin NUL, quita `</style` en cualquier caso en una pasada que también atrapa las uniones) → `customCss` → `<style id="user-css">` con `textContent`, al final del `<head>`.
  - Pruebas (RED → GREEN): con stubs, `legacyKeysKeepTheirDefaults` falló y `newTextDefaultsKeepThePreviousLook` lanzó excepción y cortó la ejecución (7 pasadas, 2 fallos); con la implementación 17/17. Cubre: claves y tipos de los preajustes ⊂ valores por defecto de cada tipo, 6 preajustes distintos, sin claves duplicadas, CSS (se conserva, no cierra el `<style>`, tope).
  - Tamaño: ~820 líneas, sobre todo tablas de valores y pruebas; no se partió para no separar la tabla de sus pruebas.
  - Checks: build macOS `BUILD SUCCEEDED`, 0 errores, 0 warnings; ctest 3/3 (test-chat 57, test-tts OK, test-overlay 17); `node --check` de las 4 páginas OK.
  - Commit: `d9a464e`.
- [x] T4 Deshacer al vaciar también en el overlay, sin voz. Route: delegated (writer).
  - `ChatManager::restored` → las fuentes de chat reciben `{type: "restore", messages: [...]}` (los últimos 50 del historial, en orden). No pasa por `ChatManager::message`, que es lo único que escucha la voz (`src/tts/voice-source.cpp:234`), así que no se vuelven a leer.
  - La página vacía la lista y el conjunto de ids vistos y vuelve a añadirlos con `add()` (respeta plataformas, máximo y «ocultar después de»); en la cinta horizontal solo los 3 últimos para no repetir una cola larga.
  - Sin prueba RED/GREEN: la conexión depende de OBS y la página no tiene runner de pruebas; `node --check` OK y prueba manual en el checklist de T5.
  - Checks: build macOS `BUILD SUCCEEDED`, 0 errores, 0 warnings.
- [ ] T5 README, compilación e instalación local en macOS. Route: delegated (writer).

## Progress
- Rama creada desde `feat/dock-ux` (63c01d1). Bloque 3 probado por el usuario en OBS: funciona.

## Next step
T1–T5 con un writer delegado.
