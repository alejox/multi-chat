# Code signing policy

*Política de firma de código — en español más abajo.*

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org).

## Status

AC Multichat has applied to SignPath Foundation's program for open source projects. Until the application is approved, the Windows builds are **not signed**. This page will say so once they are.

## What is signed

Only the files of this repository's Windows release, built from this repository's source code by its GitHub Actions workflows (`.github/workflows/build.yml` and `release.yml`):

- the plugin library `obs-multichat.dll`.

The built-in updater installs new versions on Windows with a PowerShell script that it writes at that moment; it is not a file of the release, so it is not signed.

Nothing built on a personal computer is signed, and no third-party files are signed with this certificate.

## Team roles

| Role | Who |
|---|---|
| Committers and reviewers | [Alejandro Grajales (@alejox)](https://github.com/alejox) |
| Approvers (each signing request) | [Alejandro Grajales (@alejox)](https://github.com/alejox) |

Every change reaches the `main` branch only through the maintainer, and every release is triggered from `main`. The maintainer's GitHub and SignPath accounts use multi-factor authentication. Part of the code is written with the help of an AI coding assistant; the maintainer reviews and tests every change before it is released (see [How this plugin is made](README.md#-how-this-plugin-is-made)).

## Privacy

This program does not collect telemetry and sends no information to the author. It only connects to other systems to do what the user sets it up to do:

- the chat platforms the user adds (Twitch, YouTube, Kick, TikTok, Starvios), to read their public chats and viewer counts;
- [Euler Stream](https://www.eulerstream.com), only for TikTok channels, to sign the connection to TikTok;
- the GitHub API, to check this repository's latest release, and GitHub, to download it only if the user accepts.

Details are in the [Privacy section of the README](README.md#-privacy).

---

# Política de firma de código

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org).

**Estado:** AC Multichat solicitó entrar al programa de SignPath Foundation para proyectos de código abierto. Hasta que se apruebe, las versiones de Windows **no van firmadas**.

**Qué se firma:** solo los archivos de la versión de Windows de este repositorio, compilados desde este código por sus flujos de GitHub Actions: la librería del plugin `obs-multichat.dll`. El script de PowerShell con el que el actualizador instala una versión nueva se escribe en ese momento y no forma parte de la versión, así que no va firmado. No se firma nada compilado en un equipo personal ni archivos de terceros.

**Roles:** Alejandro Grajales ([@alejox](https://github.com/alejox)) hace, revisa y aprueba cada cambio y cada solicitud de firma, con verificación en dos pasos en GitHub y SignPath. Parte del código se escribe con ayuda de un asistente de programación con IA; el mantenedor revisa y prueba cada cambio antes de publicarlo.

**Privacidad:** el programa no recoge datos de uso ni envía información al autor. Solo se conecta a otros sistemas para lo que el usuario configura: las plataformas de chat que añade, Euler Stream (solo para TikTok) y GitHub para buscar y, si el usuario acepta, descargar actualizaciones. Más detalle en la [sección de privacidad del README](README.es.md#-privacidad).
