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
- [ ] T2 Tipografía (familia, peso, colores, contorno, sombra, interlineado) en páginas y propiedades. Route: delegated (writer).
- [ ] T3 Presets con pruebas del mapeo + CSS personalizado. Route: delegated (writer).
- [ ] T4 Deshacer al vaciar también en el overlay, sin voz. Route: delegated (writer).
- [ ] T5 README, compilación e instalación local en macOS. Route: delegated (writer).

## Progress
- Rama creada desde `feat/dock-ux` (63c01d1). Bloque 3 probado por el usuario en OBS: funciona.

## Next step
T1–T5 con un writer delegado.
