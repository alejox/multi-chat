# Feature: quality

## Objective
Cerrar los pendientes de calidad de la auditoría y de los bloques 1–5 para que el plugin se sienta sólido en cualquier idioma y sistema: textos traducidos, actualizaciones verificadas y sin cabos sueltos conocidos.

## Problem and why
- Textos fijos en español dentro del código (no pasan por `data/locale`): eventos del sistema que salen en pantalla (`tiktok.cpp:238` "envió", `starvios.cpp:462-491` raid, canje, regalos, calabazas, "Anónimo") y errores (`youtube.cpp:84,313`, `tiktok.cpp:391`, `starvios.cpp:247-260,673`). Un usuario en inglés los ve en español.
- Actualizador (`src/updater.cpp`): descarga el zip del release y lo instala con permisos de administrador sin verificar integridad.
- Overlay de chat (`data/overlay/index.html`): `img.src = p.url` sin validar el esquema (`:162`); `seen.clear()` al pasar de 2000 (`:206`) deja pasar duplicados al reconectar.
- La voz lee un mensaje ya borrado si estaba en cola (limitación del bloque 2).
- Los chips del dock siguen con emoji 🔴 y 👁 (limitación del bloque 3).
- `ProfileCache` no distingue "no existe" de "error de red" (limitación del bloque 5).

## Scope
- i18n de eventos y errores: los conectores dejan de construir frases; producen una clave y argumentos (p. ej. `systemKey` + `systemArgs`) o un código de error, y la traducción se hace con `obs_module_text` en la capa de UI/overlay. Las pruebas siguen sin depender de libobs (traductor inyectable o formateo en inglés por defecto). Claves nuevas en `en-US.ini` y `es-ES.ini`.
- Actualizador: verificar SHA-256 del archivo descargado antes de instalar. Fuente del hash: el campo `digest` de los assets en la API de releases de GitHub si existe (comprobarlo con `gh api` sobre el último release de este repo) y, además, un `SHA256SUMS` publicado por `release.yml`. Si el hash no coincide o no se puede obtener, no se instala y se informa al usuario.
- Overlay: imágenes solo http/https; deduplicación acotada sin vaciar de golpe (p. ej. cola FIFO de ids).
- Voz: cancelar los mensajes en cola cuyo id fue borrado u ocultado.
- Chips: íconos SVG (en vivo, espectadores) teñidos con la paleta, como el resto del dock.
- `ProfileCache`: distinguir "no existe" de "error de red"; en error de red el diálogo dice "No se pudo comprobar" y no cachea el resultado.

## Out of scope
- Firma de código y notarización (decidido: nada orientado a cobrar por ahora).
- Nuevos idiomas.

## Constraints
- Pruebas: `cmake -S tests -B build_tests_brew -DCMAKE_PREFIX_PATH=$(brew --prefix qt) && cmake --build build_tests_brew -j8 && ctest --test-dir build_tests_brew --output-on-failure`. Base: test-chat 61, test-tts 48, test-overlay 17.
- Compilación macOS sin warnings (`-Werror`). No enlazar módulos Qt nuevos.
- El actualizador de Windows no se puede ejecutar aquí: la verificación de hash se extrae a una función pura con pruebas.

## Delivery
- Rama `feat/quality`, apilada sobre `feat/settings-dialog` (e8f5328). Sin push.
- Forecast: 600–900 líneas de autor.
- Native review: RDD apagado globalmente.

## Tasks
- [x] T1 i18n de eventos del sistema y errores de conectores, con pruebas. Route: delegated (writer; trigger: 2+ archivos no triviales).
  - Diseño: `chat::Text {key, args}` en `message.hpp`. Los conectores lanzan `ChatError(Text)` y reportan `Text`; los mensajes llevan `systemKey`/`systemArgs`/`authorKey` y `system`/`author` en inglés. `ChatManager` traduce en el hilo principal con `obs_module_text` (`localizeMessage`, `translate`), con reserva en inglés. Overlay: insignia de miembro y mensajes de ejemplo llegan traducidos en la config.
  - Base: e8f5328. RED observado: enlace fallido por `chat::fillArgs/translate/localizeMessage/englishTexts` sin definir; luego `localeFilesHaveEveryText` falló (faltaba `Author.Anonymous` en en-US.ini). GREEN: test-chat 65/65, test-tts 48, test-overlay 17; build macOS BUILD SUCCEEDED, 0 errores, 0 warnings.
- [x] T2 Verificación SHA-256 en el actualizador + `SHA256SUMS` en `release.yml`, con pruebas de la función pura. Route: delegated (writer).
  - Evidencia `gh api repos/alejox/multi-chat/releases/latest`: v2.1.1 trae `digest: "sha256:<hex>"` en los tres assets. Decisión: hash del `digest` del asset; si falta, el `SHA256SUMS` de la Release; sin hash o con hash distinto no se instala (`Update.NoChecksum` / `Update.VerifyFailed`). Textos de detalle del actualizador pasados a `data/locale`.
  - T1 en 3e6410f. RED observado: test-chat no compilaba (`Release::sha256/assetName/sumsUrl`, `sha256FromDigest` sin declarar). GREEN: `releaseChecksums` pasa; test-chat 66, test-tts 48, test-overlay 17; build macOS BUILD SUCCEEDED sin errores ni warnings; YAML de release.yml válido.
  - Sin verificar aquí: rutas de Windows y Linux del actualizador (no compilan en macOS) y el paso nuevo de release.yml (corre en la próxima Release).
- [ ] T3 Overlay (esquema de imágenes, dedupe acotado) y voz (cancelar borrados). Route: delegated (writer).
- [ ] T4 Chips con SVG y `ProfileCache` con error de red. Route: delegated (writer).
- [ ] T5 README, compilación e instalación local en macOS. Route: delegated (writer).

## Progress
- Rama creada desde `feat/settings-dialog` (e8f5328). Bloque 5 probado por el usuario en OBS: funciona.

## Next step
T1–T5 con un writer delegado.
