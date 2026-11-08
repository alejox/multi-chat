# Feature: dock-ux

## Objective
Llevar el dock "Multichat" a calidad premium: lectura cómoda con mucho tráfico, acciones por mensaje, búsqueda, íconos y colores coherentes con cualquier tema de OBS, accesible por teclado.

## Problem and why
Auditoría (snapshot de `main`):
- No hay aviso de mensajes nuevos cuando el usuario sube; `rebuild()` fuerza el scroll al final al tocar un chip o cambiar el tema.
- Cada imagen cargada marca sucio todo el documento (coste con 300 mensajes).
- No hay menú contextual, búsqueda ni hora por mensaje; 🗑 borra sin confirmar.
- Botones de cabecera con glifos emoji ("A−", "⟳", "🗑", "⚙") que se ven distintos en cada sistema; colores fijos (`#ff6b6b`, `rgba(145,70,255,70)`) con mal contraste en el tema claro.
- Los chips se recrean en cada `stateChanged` (pierden el foco, parpadean); no hay `setAccessibleName`.
- El usuario no puede probar la moderación con mensajes de prueba porque no existen en la plataforma: pidió "Ocultar mensaje" local.

## Scope
- Píldora "↓ N nuevos" al estar desplazado hacia arriba; clic o Fin vuelve al final. `rebuild()` conserva la posición si el usuario no estaba al final.
- Invalidar solo el bloque afectado al cargar una imagen.
- Menú contextual por mensaje: Destacar / Quitar destacado, Ocultar mensaje (local: usa la misma ruta de borrado que la moderación, con `source` y `id`, sin tocar la plataforma), Ocultar usuario (lo añade a los usuarios ocultos de Filtros), Copiar mensaje, Copiar nombre.
- Búsqueda: Ctrl+F muestra una barra; filtra por texto o autor (sin tildes ni mayúsculas, reutiliza `foldText`); Esc la cierra.
- Hora opcional por mensaje (preferencia del dock, desactivada por defecto).
- Vaciar chat: deshacer durante unos segundos (barra "Chat vaciado · Deshacer"), sin diálogo modal.
- Íconos SVG monocromos empaquetados, coloreados con la paleta del tema; colores de error, banner y barra derivados de `palette()`. Probar con temas oscuros y el claro.
- Chips actualizados en sitio por clave; `setAccessibleName`/tooltips en botones y chips; atajos Ctrl+F, Fin, Esc.
- Textos en `en-US.ini` y `es-ES.ini`; README.
- Pruebas en `tests/` para la lógica pura nueva (p. ej. coincidencia de búsqueda).

## Out of scope
- Moderar en la plataforma (borrar o banear de verdad).
- Rediseño del diálogo de configuración (bloque 5) y de las propiedades de fuentes (bloque 4).

## Constraints
- Pruebas locales: `cmake -S tests -B build_tests_brew -DCMAKE_PREFIX_PATH=$(brew --prefix qt) && cmake --build build_tests_brew -j8 && ctest --test-dir build_tests_brew --output-on-failure` (Makefile; no hay Ninja). RED antes de GREEN cuando haya lógica pura probable.
- Compilación macOS sin warnings.
- El texto del chat sigue siendo no confiable: escapado en el HTML del dock.

## Delivery
- Rama `feat/dock-ux`, apilada sobre `feat/chat-moderation` (0e34464). Sin push.
- Forecast: 600–900 líneas de autor.
- Native review: RDD apagado globalmente.

## Tasks
- [x] T1 Scroll: píldora de nuevos, `rebuild()` sin saltos, invalidación por bloque. Route: delegated (writer). Commit: f5e013e. Checks: build macOS OK sin warnings; pruebas 54/54 + tts OK. Sin RED/GREEN: comportamiento de UI sin prueba ejecutable (QTextBrowser), se verifica a mano. Además compensa el desplazamiento cuando se descarta el mensaje más viejo (límite de 300) estando arriba.
- [x] T2 Menú contextual (Destacar, Ocultar mensaje, Ocultar usuario, Copiar) y deshacer al vaciar. Route: delegated (writer). Commit: 3d1e70e. Checks: RED observado (`localModeration`, `plainTextCopy` fallaron con stubs), GREEN 56/56 + tts OK; build macOS sin warnings. Ocultar mensaje/usuario usan `chat::hideMessage`/`hideAuthor` → `applyDeletion` (dock, overlay y destacado). Ocultar usuario guarda el filtro y avisa al diálogo de configuración abierto (`filtersChanged`) para no perderlo. Deshacer restaura historial y dock; el overlay no se restaura (overlay-source fuera del alcance de este bloque; reenviar `message` dispararía TTS). Copiar selección da solo texto plano.
- [x] T3 Búsqueda Ctrl+F y hora opcional, con pruebas de la lógica de búsqueda. Route: delegated (writer). Commit: 6e75255. Checks: RED observado (`searchMatching` falló con stub), GREEN 57/57 + tts OK; build macOS sin warnings. `chat::matchesSearch` (todas las palabras, en autor, texto, emotes o línea de sistema, con `foldText`). Hora: preferencia `timestamps` en dock.json, formato corto del sistema; se activa desde el menú contextual. La hora no tiene prueba (depende de la zona horaria local).
- [x] T4 Íconos SVG, colores del tema, chips incrementales, accesibilidad y atajos. Route: delegated (writer). Checks: build macOS sin warnings; pruebas 57/57 + tts OK; ícono en el bundle (`Resources/icons`). Sin RED/GREEN: UI sin prueba ejecutable. Íconos en `data/icons/*.svg` cargados con `QIcon` (plugin de íconos SVG que trae OBS) y teñidos con `ButtonText`; no se enlaza Qt6::Svg porque el CI de Linux solo instala `qt6-base-dev` y el workflow está fuera del alcance. Banner, aviso de versión, errores y "En vivo" derivan de `palette()`; textos de ayuda del diálogo usan `PlaceholderText`. Chips por clave sin recrearse; nombres accesibles en botones, chips, estado y búsqueda. Atajos: Ctrl+F, Fin, Esc (cierra búsqueda, luego quita destacado).
- [ ] T5 README, compilación e instalación local en macOS. Route: delegated (writer).

## Progress
- Rama creada desde `feat/chat-moderation` (0e34464). Bloque 2: pruebas 54/54 OK; moderación real pendiente de probar por el usuario con un chat real (los mensajes de prueba no existen en la plataforma).

## Next step
T1–T5 con un writer delegado.
