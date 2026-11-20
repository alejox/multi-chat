# Changelog

Each `## <version>` section becomes the description of that release, and the plugin's updater shows
its items before installing (the Spanish ones when OBS is in Spanish, the English ones otherwise).

## 2.5.0

### Español

- **Nuevo:** chat de YouTube al instante. Si inicias sesión con Google (Ajustes → YouTube), YouTube envía cada mensaje apenas se escribe, en vez de consultarse cada 2 segundos. Es opcional y basta con un clic: no hay que crear nada en Google Cloud (quien lo prefiera puede usar su propia aplicación).

### English

- **New:** instant YouTube chat. If you sign in with Google (Settings → YouTube), YouTube sends each message as soon as it is written, instead of being checked every 2 seconds. It is optional and takes one click: there is nothing to create in Google Cloud (you can still use your own application if you prefer).

## 2.4.2

### Español

- **Error corregido:** los mensajes de Starvios no aparecían. Starvios cambió la forma en que reparte su chat y el plugin ya escucha la nueva; siguen llegando al instante.

### English

- **Bug fixed:** Starvios messages were not showing up. Starvios changed how it delivers its chat and the plugin now listens to the new way; they still arrive instantly.

## 2.4.1

### Español

- **Error corregido:** en YouTube los mensajes tardaban en aparecer o el chat parecía dejar de leerlos.
- Un fallo momentáneo de YouTube ya no cierra el chat y, tras un corte, se recuperan los mensajes escritos mientras tanto.
- El chat de YouTube se consulta cada 2 segundos (antes cada 6) y se leen todos los mensajes, no solo los destacados.
- **Nuevo:** este aviso muestra qué trae cada versión.

### English

- **Bug fixed:** on YouTube, messages were slow to show up or the chat seemed to stop reading them.
- A momentary YouTube failure no longer closes the chat and, after an outage, messages written meanwhile are recovered.
- YouTube chat is checked every 2 seconds (it was every 6) and every message is read, not only the top ones.
- **New:** this notice shows what each version brings.
