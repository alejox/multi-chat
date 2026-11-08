# Feature: chat-moderation

## Objective
Que un mensaje borrado por la plataforma, o los mensajes de un usuario baneado o expulsado temporalmente, desaparezcan del dock, del overlay de chat y del mensaje destacado. Y que el streamer pueda ocultar bots, comandos y palabras.

## Problem and why
Hoy Twitch solo procesa `USERNOTICE`: `CLEARMSG` y `CLEARCHAT` se ignoran, así que un mensaje moderado sigue al aire. Para un plugin con calidad premium esto es inaceptable: un insulto borrado por un moderador no puede quedar en pantalla.

## Scope
- Modelo: evento de moderación `{type: delete, platform, source, id?, author?, all?}`.
- Conectores (si la plataforma lo expone, con evidencia en el código o la documentación):
  - Twitch IRC: `CLEARMSG` (por `target-msg-id`), `CLEARCHAT` con usuario (timeout/ban) y sin usuario (vaciar todo el canal).
  - YouTube: acciones de borrado del live chat (`markChatItemAsDeletedAction`, `markChatItemsByAuthorAsDeletedAction`, `removeChatItemAction`, `removeChatItemByAuthorAction` o las que use el parser actual).
  - Kick: eventos de Pusher de mensaje borrado y usuario baneado.
  - TikTok y Starvios: solo si el protocolo lo trae; si no, se documenta.
- `ChatManager`: aplica el evento a `history_`, emite una señal; si afecta al destacado, lo quita.
- Dock: quita los bloques afectados sin mover el scroll.
- Overlay de chat (`index.html`): quita los mensajes afectados con la animación de salida.
- Filtros: lista de usuarios ocultos (por defecto bots conocidos: Nightbot, StreamElements, Streamlabs, Moobot, Fossabot, Wizebot, Botrix), ocultar comandos que empiezan por `!`, lista de palabras bloqueadas (coincidencia por palabra, sin distinguir mayúsculas ni tildes). Se configuran en el diálogo "Multichat: configurar chats" y se aplican en `ChatManager` antes de guardar o emitir.
- Textos en `en-US.ini` y `es-ES.ini`; README.
- Pruebas en `tests/test-chat.cpp` con fixtures para los parsers nuevos.

## Out of scope
- Moderar desde el plugin (borrar o banear en la plataforma).
- Filtros por expresiones regulares o por plataforma.

## Constraints
- QtTest no configura en macOS con `.deps` (falta Qt6Test): las pruebas se escriben y las corre el CI de Ubuntu; localmente se verifica la compilación.
- Compilación macOS sin warnings.
- La moderación nunca debe borrar mensajes de otra fuente o canal: el borrado siempre se limita a `source`.

## Delivery
- Rama `feat/chat-moderation`, apilada sobre `feat/featured-messages` (6c93ea1). Sin push.
- Forecast: 500–700 líneas de autor.
- Native review: RDD apagado globalmente.

## Tasks
- [x] T1 Modelo + Twitch `CLEARMSG`/`CLEARCHAT` + `ChatManager` + dock + overlay + destacado, con pruebas. Route: delegated (writer; varios archivos no triviales).
  - Diseño: el evento de borrado viaja por el mismo `PostFn` que los mensajes (mismo `QueuedConnection`); `ChatManager::receive` lo separa con `chat::isDeletion`, quita del historial, añade `ids`, quita el destacado (y el "último destacado") si coincide y emite `deleted`. `chat::deletionMatches` es pura y exige mismo `source` y `platform`.
  - Twitch: los mensajes ya usan la etiqueta IRC `id`; ahora guardan `authorId` (`user-id`). `CLEARMSG` → `target-msg-id`; `CLEARCHAT #c :login` → `target-user-id` + login; `CLEARCHAT #c` → todo el canal.
  - Checks: compilación macOS OK, 0 warnings. Con Qt de Homebrew (instalado después de escribir T1): `test-chat twitchModeration deletionByAuthorName` → 4 passed, 0 failed (GREEN observado). RED no observado: la implementación se escribió antes de tener el runner local.
  - Commit: `f299f97`.
- [x] T2 YouTube, Kick (y TikTok/Starvios si aplica), con pruebas de parser. Route: delegated (writer).
  - YouTube: `markChatItemAsDeletedAction`/`removeChatItemAction` (`targetItemId` = `id` del renderer) y `markChatItemsByAuthorAsDeletedAction`/`removeChatItemByAuthorAction` (`externalChannelId` = `authorExternalChannelId`, que ahora se guarda como `authorId`). No hay acción de "vaciar todo".
  - Kick (Pusher, `chatrooms.<id>.v2`): `App\Events\MessageDeletedEvent` (`message.id`), `App\Events\UserBannedEvent` (`user.id`, `user.username`; ban y timeout) y `App\Events\ChatroomClearEvent` (todo el canal). Los mensajes guardan `sender.id` como `authorId`.
  - Starvios: la difusión `{op, msg}` con `op` `DELETE`, o `UPDATE` con `msg.is_deleted`, quita el mensaje `msg.id`. Sin evidencia de baneos ni de vaciado.
  - TikTok: no soportado; el parser no guarda id de mensaje ni lee ningún mensaje de borrado del protocolo.
  - Checks: RED observado con stubs vacíos (`kickModeration`, `youtubeModeration`, `starviosModeration`: 3 failed); GREEN: `test-chat` 49 passed, 0 failed; `ctest` 2/2. Compilación macOS OK, 0 warnings.
  - Commit: `2aa932c`.
- [x] T3 Filtros de bots, comandos y palabras, con UI en el diálogo de configuración y pruebas. Route: delegated (writer).
  - Lógica pura en `chat::FilterRules`, `chat::foldText` (NFD sin marcas + case fold) e `chat::isFiltered` (`src/chat/message.cpp`). La regla de comandos (`^\s*!\S`) solo aplica a `kind == "chat"`; usuarios ocultos y palabras aplican a todo.
  - Persistencia: clave `filters` del mismo `config.json` que los chats (`ChatManager::load/save`); sin la clave se usan los valores por defecto. `ChatManager::receive` filtra antes del historial y de emitir (dock, overlay y TTS no lo ven).
  - UI: pestañas "Chats" y "Filtros" en el diálogo; listas de una entrada por línea (o separadas por comas), botón "Añadir bots conocidos", guardado 400 ms después de cada cambio y al cerrar.
  - Checks: RED observado con stubs (5 failed: `filterFold`, `filterHiddenUsers`, `filterCommands`, `filterBlockedWords`, `filterJson`); GREEN: `test-chat` 54 passed, 0 failed; `ctest` 2/2. Compilación macOS OK, 0 warnings.
  - Commit: `fece4f9`.
- [x] T4 README, compilación e instalación local en macOS. Route: delegated (writer).
  - README: filas de Moderación y Filtros en Funciones, sección "Moderación y filtros" con la tabla por plataforma.
  - Checks: `cmake --preset macos && cmake --build --preset macos` → BUILD SUCCEEDED, 0 warnings. `ctest` 2/2 (`test-chat` 54 passed). OBS no estaba abierto (`pgrep -x OBS` sin resultado): plugin instalado con `ditto` en `~/Library/Application Support/obs-studio/plugins/obs-multichat.plugin` (locale y overlay nuevos verificados en el paquete).
  - Commit: este mismo (`docs(moderation)`).

## Progress
- Rama creada desde `feat/featured-messages` (6c93ea1). Bloque 1 probado por el usuario en OBS: funciona.

## Next step
Prueba manual en OBS (lista en el informe del writer). Push y PR los decide el usuario.
