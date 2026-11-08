# Feature: phase-c (Fase C)

## Objective
Cerrar los últimos pendientes de la revisión de arquitectura: una fuente menos para el usuario (el reloj pasa a ser una opción del chat), medición de cajas segura entre hilos y un actualizador más fácil de mantener.

## Problem and why
- Hay 5 tipos de fuente; el reloj ya existe dentro de la página del chat (la cinta lo muestra con `updateClock`) y además como fuente propia (`multichat_clock`, `clock.html`). El usuario ve dos relojes distintos.
- `ov_update` puede llamarse desde cualquier hilo y mide las cajas de espectadores/reloj con `QFont`/`QFontMetricsF` fuera del hilo de UI: riesgo en Windows y Linux.
- `src/updater.cpp` mezcla consulta de versiones, diálogos y tres instaladores, con un script de PowerShell en un literal.

## Scope
- T1 Reloj como opción del chat: en la fuente "Multichat" un grupo "Fecha y hora" que funciona en vertical y horizontal (hoy solo en la cinta). La fuente `multichat_clock` se sigue registrando con `OBS_SOURCE_CAP_DEPRECATED` para que las escenas existentes carguen y se vean igual, pero no aparece en "Añadir fuente". Nada se borra de las escenas del usuario. Asistente, README y presets coherentes.
- T2 Medición en el hilo de UI: el cálculo del tamaño de las cajas se hace en el hilo principal (cola a `QCoreApplication`/`QMetaObject::invokeMethod`), y `ov_update` solo guarda la configuración y pide la medición. Prueba de la función de medición pura con QtGui en `tests/` si es posible sin OBS.
- T3 Actualizador: separar `updater/check` (consulta y verificación), `updater/install-win.cpp`, `install-mac.cpp`, `install-linux.cpp` y el script de PowerShell como recurso o constante en su propio archivo. Movimientos sin cambio de comportamiento; las pruebas existentes de checksum siguen igual.
- T4 Compilación, pruebas, instalación local, README.

## Out of scope
- Nuevas funciones. Cambios en conectores.

## Constraints
- Base: lo que deje la Fase B (`refactor/internal-cleanup`). Mismas reglas de pruebas, `-Werror`, sin módulos Qt nuevos, sin push.

## Delivery
- Rama `feat/phase-c`, apilada sobre `refactor/internal-cleanup`. Sin push.

## Tasks
- [x] T1 Reloj como opción del chat, `multichat_clock` obsoleta pero compatible. Route: delegated (writer). Base: a50cb5f.
  - Grupo "Fecha y hora" del chat visible en vertical y horizontal; claves `clock`, `clock_*` sin renombrar; nueva `clock_position` (`top`/`bottom`, solo vertical, por defecto `top`).
  - `multichat_clock` se registra con `OBS_SOURCE_DEPRECATED | OBS_SOURCE_CAP_OBSOLETE` (en OBS 33 no existe `OBS_SOURCE_CAP_DEPRECATED`; `OBS_BasicSourceSelect` oculta los tipos con `OBS_SOURCE_CAP_DISABLED` = `CAP_OBSOLETE`, y libobs los sigue creando al cargar escenas). Mismo id, callbacks y defaults: las escenas guardadas cargan igual.
  - El asistente y "Añadir fuentes" nunca añadían el reloj (`ui-common.cpp` solo añade chat, destacado y voz): sin cambios.
  - Pruebas: `chatClockHasAVerticalPosition` RED observado (falta `clock_position`) → GREEN; test-overlay 20/20; 5/5 suites; build macOS BUILD SUCCEEDED sin avisos.
- [ ] T2 Medición de cajas en el hilo de UI. Route: delegated (writer).
- [ ] T3 Separar el actualizador. Route: delegated (writer).
- [ ] T4 Compilación, pruebas, instalación y README. Route: delegated (writer).

## Next step
Arranca cuando termine la Fase B.
