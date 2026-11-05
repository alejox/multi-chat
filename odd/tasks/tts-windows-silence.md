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
- [x] T1 RED: prueba real de SAPI en el CI de Windows. Route: delegated (writer; toca 3 archivos no triviales). Commits: `8e3ef7e`, `a030593`.
  - Accept: `test-tts-win` corre en el job de Windows, falla con límite de tiempo, y distingue "sin voces" de un fallo.
  - Check: run de CI en rojo con la salida real.
- [x] T2 GREEN: arreglar el backend. Route: delegated (writer). Commits: `11fdfb6`, `5596bb5`.
  - Accept: formato explícito, PCM directo, HRESULT informados, cancelación igual que antes, `wav.hpp` retirado si queda sin uso.
  - Check: los 4 jobs de CI en verde con `test-tts-win` ejecutada en Windows.
- [x] T3 Comprobación local. Route: delegated (writer). Commit: ninguno.
  - Check: `ctest --test-dir build_tests`; `cmake --build --preset macos`.

## Verification evidence
- Run 41976314580 (`8e3ef7e`): el job de Windows falló al compilar la prueba, no por la voz: C2589 en `std::max` por la macro de `windows.h`. Se corrigió con `NOMINMAX` en `a030593`.
- T1 RED, run https://github.com/alejox/multi-chat/actions/runs/41976727752 (`a030593`), job de Windows en rojo; `tests` y `linux` en verde al leerlo. Salida real:
  - `voces en este sistema: 2` (Microsoft David Desktop y Microsoft Zira Desktop, en-US).
  - Sonda con formato nulo, lo que hacía el backend: `SetBaseStream hr=0x80070057`.
  - Sonda con PCM 16 bits mono a 22050 Hz: `SetBaseStream`, `SetOutput` y `Speak` en `hr=0x00000000`; `bytes=153870`; `primeros 4 bytes: 00 00 00 00` (sin RIFF).
  - Voz predeterminada, David, Zira y voz inexistente: `llamadas PCM=0 ... FALLO: terminó sin entregar audio`. Texto vacío: `FALLO: terminó sin audio y sin ningún aviso en el log`. `stop()`: ok.
- Causa observada: las dos hipótesis son ciertas. `SetBaseStream` rechaza el formato nulo con `E_INVALIDARG` y el error se tragaba; con formato válido el stream recibe PCM crudo, que `fromWav` habría rechazado por no llevar RIFF.
- Primer intento de GREEN, run https://github.com/alejox/multi-chat/actions/runs/41977150129 (`11fdfb6`): ya había audio (`frecuencia=22050 canales=1 muestras=76935 duracion=3.49s pico=0.881`), pero la prueba falló porque `SetOutput(nullptr, TRUE)` devolvía `0x8004503A` en el runner y la prueba no admite avisos en un caso correcto. El audio ya estaba completo antes de esa llamada, así que `5596bb5` la quita.
- T2 GREEN, run https://github.com/alejox/multi-chat/actions/runs/41977505057 (`5596bb5`): `tests`, `macos`, `windows` y `linux` en `success`. Salida de `test-tts-win`:
  - Voz predeterminada y David: `frecuencia=22050 canales=1 muestras=76935 duracion=3.49s pico=0.881`. Zira: `muestras=77816 duracion=3.53s pico=0.679`.
  - Voz inexistente: audio y aviso `no existe la voz ...; se usa la predeterminada`. Texto vacío: aviso `un texto terminó sin audio`. `stop()`: sin audio.
  - `RESULTADO: todo bien`.
- `wav.hpp` quedó sin uso y se retiró con sus 5 pruebas de `test-tts.cpp`.
- T3 en `11fdfb6`: `ctest --test-dir build_tests` 100% (2 de 2), contra el QtTest stand-in local de `build_tests`; el QtTest real corre en el job `tests`. `cmake --build --preset macos`: BUILD SUCCEEDED. `5596bb5` solo cambia un archivo de Windows, que aquí no se compila.
- No verificado: que OBS reproduzca ese PCM en la máquina del usuario. El CI demuestra que el backend produce audio, no que se oiga, ni que el tono sea correcto de oído.

- Revisión nativa: apagada a nivel global. `gentle-ai review assess` dio riesgo alto (`shell_source` en `build.yml`), así que se usó un verificador independiente de solo lectura: PASS, sin defectos bloqueantes.
- Hallazgos menores sin corregir: la prueba no detecta audio truncado por encima de 0,2 s ni cancela durante la síntesis; el hilo separado podría sobrevivir a la descarga del plugin (ya existía antes de este cambio).

## Next step
Publicar 2.1.1. Después, el usuario prueba de oído en Windows; si sigue sin sonar, el log de OBS debería mostrar líneas `[multichat] voz de Windows`.
