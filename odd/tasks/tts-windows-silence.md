# Feature: tts-windows-silence

## Objective
Que la fuente "Multichat (voz)" lea los mensajes en Windows, y que cualquier fallo de síntesis quede en el log de OBS en lugar de perderse.

## Problem and why
La versión 2.1.0 añadió la voz en los tres sistemas. En Windows el usuario no oye nada; en macOS funciona. El CI solo compilaba `speech-backend-win.cpp`, nunca lo ejecutaba, así que nada lo detectó.

Hipótesis sin demostrar al crear este documento, en `src/tts/speech-backend-win.cpp`:
1. `SetBaseStream(memStream, SPDFID_WaveFormatEx, nullptr)` pasa un formato nulo con un id que lo necesita; si falla, el error se traga.
2. `fromWav` exige una cabecera RIFF, y un `ISpStream` sobre un `IStream` puede escribir PCM crudo sin cabecera.

Los HRESULT de `SetOutput`, `Speak` y `WaitUntilDone` se ignoran, así que todo fallo es invisible.

## Scope
- `tests/test-tts-win.cpp`: prueba solo de Windows que ejecuta el backend real con `createSpeechBackend()`.
- `CMakeLists.txt`: objetivo `test-tts-win`, solo en Windows y fuera de `ALL`.
- `.github/workflows/build.yml`: compilar y ejecutar esa prueba en el job de Windows.
- `src/tts/speech-backend-win.cpp`: formato explícito, PCM directo y HRESULT revisados.
- `src/tts/wav.hpp`, `tests/test-tts.cpp`: quitar el parser si queda sin uso.

## Out of scope
- Enumerar voces OneCore y la calidad de la voz.
- Subir la versión, tocar `main`, tags, releases, PR y `release.yml`.

## Constraints
- La prueba no usa QtTest: el Qt que descarga el job de Windows (`.deps`) no lo trae, y el job no debe cambiar sus dependencias.
- El backend no puede depender de OBS: los fallos se informan con `qWarning("[multichat] ...")`, como `src/chat/starvios.cpp`. OBS 33 instala un `qInstallMessageHandler` que manda los avisos de Qt a `blog(LOG_WARNING)` (`frontend/obs-main.cpp:622`).
- Se conservan el hilo por trabajo, `Job`, `detach` y `stop()`, y la compilación limpia con /W4 /WX.
- Windows no compila en esta máquina (macOS): la verificación es el CI.

## Delivery
- Forecast: unas 300 líneas de autor, menos el parser si se retira.
- Strategy: `single-pr`. Push a `fix/windows-tts-silence`; merge, versión y release quedan fuera de esta tarea.
- Native review: la decide quien coordina la tarea, no este trabajo.

## Tasks
- [ ] T1 RED: prueba real de SAPI en el CI de Windows. Route: delegated (writer; toca 3 archivos no triviales). Commit: pendiente.
  - Accept: `test-tts-win` corre en el job de Windows, falla con límite de tiempo, y distingue "sin voces" de un fallo.
  - Check: run de CI en rojo con la salida real.
- [ ] T2 GREEN: arreglar el backend. Route: delegated (writer). Commit: pendiente.
  - Accept: formato explícito, PCM directo, HRESULT informados, cancelación igual que antes, `wav.hpp` retirado si queda sin uso.
  - Check: los 4 jobs de CI en verde con `test-tts-win` ejecutada en Windows.
- [ ] T3 Comprobación local. Route: delegated (writer). Commit: ninguno.
  - Check: `ctest --test-dir build_tests`; `cmake --build --preset macos`.

## Verification evidence
Pendiente.

## Next step
T1: subir la prueba y observar el fallo en el CI de Windows.
