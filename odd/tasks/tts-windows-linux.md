# Feature: tts-windows-linux

## Objective
Habilitar la fuente "Multichat (voz)" en Windows y Linux, no solo en macOS, y quitar del README el aviso de "solo en macOS".

## Problem and why
La voz es una fuente de audio de OBS separada del resto del plugin. El motor (voice-engine.cpp) y la fuente (voice-source.cpp) son independientes del sistema; lo único que falta por sistema es el sintetizador (speech-backend-*). Hoy solo existe el de macOS y la fuente ni se compila en Windows ni en Linux.

## Specs
- S1 "ajusta ahora para windows y linux y sube todo a main y haz release" (autoriza voz en Windows y Linux, push, merge a main y release).
- S2 "pero el release debe hacerce con lo de windows y linux" (la release sale solo con Windows y Linux incluidos).
- S3 "saco de README.md el aviso de 'solo en macOS'" (eliminar del README las menciones de que la voz solo existe en macOS).
- S4 "Usa las voces del sistema, sin servicios externos" (constraint heredado de tts-chat-reader: voces del sistema, nada en la nube).

## Scope
- `src/tts/wav.hpp`: parseo RIFF/WAVE → PcmBuffer (compartido, testeable en macOS).
- `src/tts/speech-backend-win.cpp`: sintetizador de Windows con SAPI (voces del sistema).
- `src/tts/speech-backend-linux.cpp`: sintetizador de Linux con espeak-ng (voces del sistema).
- `CMakeLists.txt`: compilar la fuente y el backend de voz en los tres sistemas y enlazar las librerías.
- `src/plugin-main.cpp` y `src/tts/voice-source.hpp`: quitar el guard `__APPLE__`.
- `.github/workflows/build.yml`: dependencia espeak-ng en el job de Linux.
- `README.md`: quitar "solo en macOS" y documentar la voz en los tres sistemas.

## Out of scope
- Cambiar la calidad de voz ni las voces en la nube.
- Responder en el chat o iniciar sesión.

## Constraints
- El backend implementa `tts::SpeechBackend` (speak/stop + createSpeechBackend + availableVoices) sin tocar el motor ni la fuente.
- Windows y Linux no compilan en esta máquina (macOS): su única verificación es el CI de GitHub Actions.
- Los identificadores quedan en inglés; los comentarios siguen en español neutro (convención del repo).

## Delivery
- Forecast: about 600 authored changed lines.
- Strategy: `single-pr` sobre `feat/tts-chat-reader` junto con la voz de macOS y Starvios, por decisión explícita del usuario. Push, merge y release quedan a decisión del usuario.
- RDD: off (decidido global), sin native review; checks ordinarios.

## Tasks
- [x] T1 Parser RIFF/WAVE (`src/tts/wav.hpp`) con prueba. Route: inline (parent). Commit: `3ec79c7`.
  - Accept: PCM 8/16/24/32 bits y float 32/64, mono y estéreo; `fromWav` rechaza datos inválidos sin leer fuera del búfer.
  - Check: prueba nueva en test-tts (RED primero) + suite completa.
- [x] T2 Backend de Windows con SAPI (`speech-backend-win.cpp`). Route: inline (parent). Commit: `3703a3c`.
  - Accept: enumera las voces del sistema, sintetiza a PCM en memoria, respeta rate y voiceId, stop no entrega audio de un texto cancelado.
  - Check: sin runner local; lo verifica el CI de Windows. Excepción documentada (no hay SAPI en macOS).
- [x] T3 Backend de Linux con espeak-ng (`speech-backend-linux.cpp`). Route: inline (parent). Commit: `3703a3c`.
  - Accept: enumera las voces, sintetiza a PCM vía callback, respeta rate y voiceId.
  - Check: sin runner local; lo verifica el CI de Linux. Excepción documentada.
- [x] T4 Cableado de compilación y registro (`CMakeLists.txt`, `plugin-main.cpp`, `voice-source.hpp`). Route: inline (parent). Commit: `3703a3c`.
  - Accept: la fuente se registra en los tres sistemas; macOS sigue compilando sin cambios de comportamiento.
  - Check: `cmake --build --preset macos` (el guard no rompe macOS).
- [x] T5 CI y README (`build.yml`, `README.md`). Route: inline (parent). Commit: `e45664a`.
  - Accept: Linux instala espeak-ng; README ya no dice "solo en macOS" y documenta la voz en los tres sistemas con su dependencia.
  - Check: lectura estructural del README; el CI lo confirma en el próximo push.

## Verification evidence
- T1 RED: con `fromWav` como stub, `ctest` falló `wavParsesPcm16Mono`, `wavParsesPcm16Stereo`, `wavParsesPcm8` y `wavParsesFloat32` (47 pasadas, 4 fallidas). GREEN tras implementar: 100%.
- Suite final: `ctest --test-dir build_tests` 100% (test-chat 31.6 s, test-tts 1.1 s), contra el QtTest stand-in local (el Qt de `.deps` no trae QtTest).
- `cmake --build --preset macos`: BUILD SUCCEEDED, x86_64 y arm64 (el guard `__APPLE__` retirado no rompe macOS; los archivos de Win/Linux no se compilan aquí).
- `rg -i "solo en macos|voz de macOS|esta fuente no aparece" README.md src/`: sin coincidencias.
- No verificado en esta máquina: la compilación de Windows (SAPI) y Linux (espeak-ng) y el sonido real. Lo confirma el CI de GitHub Actions en el próximo push.
- CI (push de `feat/tts-chat-reader`, run 41971725471): `success` en los 4 jobs — `tests`, `macos`, `windows`, `linux`. SAPI y espeak-ng compilaron a la primera y las pruebas pasaron con el QtTest real de Ubuntu.
- Versión subida a 2.1.0 (`buildspec.json` + `manifest.json`), commit `b07bb2b`; `main` avanzó con fast-forward hasta ese commit y el push disparó el flujo *Publicar versión* (run 41972411070).

## Next step
Esperar el flujo *Publicar versión* (crea el tag `v2.1.0` y la Release con los tres paquetes). Si algo falla en la release, se revisa el job y se corrige antes de reintentar.

## Log
- L1 (2026-11-04 23:08): "ya todo quedo en orden, ajusta ahora para windows y linux y sube todo a main y haz release"
- L2 (2026-11-04 23:32): "pero el release debe hacerce con lo de windows y linux"
- L3 (2026-11-04 23:32): "cual e sel aviso de solo en macOS?" (pedido de ubicar el aviso; respondido: README.md líneas 30, 109, 113 y 181).
