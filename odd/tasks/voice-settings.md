# Feature: voice-settings

## Objective
Voice becomes a plugin setting, not a source the user manages: a "Voice" tab in the Settings dialog (Configurar) with a "Read chat aloud" checkbox and its options. The plugin owns one voice output, always audible regardless of the active scene, with audio monitoring set to "Monitor and Output". One single blocked-words list (Filters) for everything.

## Problem and why
- Two blocked-words lists (Settings → Filters and the voice source properties) with different meanings confuse users.
- The voice is a source the user must add to every scene; it only speaks in scenes that contain it and its monitoring is never set, so the streamer does not hear it.
- Putting voice inside the chat source was rejected: the chat source is video-only (`src/overlay/source.cpp:192`), and with vertical + horizontal chat sources every message would be read twice, plus a silent mixer strip per chat source.

## Decisions (user, 2026-11-08)
- Voice tab in Settings (Configurar). Monitoring default: Monitor and Output (`OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT`).
- Blocked words: only the Filters list remains; voice obeys it (messages hidden by filters are never read). The "shown but not read" case is dropped.

## Scope
- T1 Global voice settings in the plugin config (`ChatManager`/config store): `enabled` (default false until the user checks it), `mode` (read/sound/both), `voice`, `rate`, `read_author`, per-platform toggles, `max_chars`, `sound_file`, `sound_cooldown`. One-time migration from existing voice sources: if the global voice section is absent, take values from the first `multichat_voice*` source with user-set values (v2 untouched counts as enabled=true, v0 untouched as false); voice-source `blocked_words` are appended to the Filters blocked words (deduplicated). Voice-source blocked-words field removed. Pure logic with tests.
- T2 Plugin-owned voice output: one non-private source "Multichat (voz)" (the existing voice source type, latest version) bound to a free global output channel (`obs_set_output_source`, verify free channel range in libobs; never clobber channels 1–6) so it is audible in every scene and shows in the mixer. Created when the global voice is enabled (at load and when the checkbox is checked), released on disable/unload. On creation set monitoring to Monitor and Output. Persist its audio state (volume, mute, monitoring, balance, filters via `obs_source_save`/`obs_load_source` or equivalent) in the plugin config so user mixer changes survive restarts; first creation uses Monitor and Output. Voice engine reads global settings and reacts to changes. Both voice source versions are hidden from "Add source" (`OBS_SOURCE_CAP_OBSOLETE`); voice sources already saved in scenes load but stay silent, and their properties show only a note: "Voice is now configured in Multichat → Settings → Voice; you can delete this source."
- T3 UI: "Voice" tab in `SettingsDialog` (checkbox "Read chat aloud", mode, voice, rate, read author, platforms, max chars, sound file, cooldown, "Skip current message"), short help that blocked words live in Filters and that volume/monitoring are in the OBS mixer. Wizard sources step and "Add sources to scene" no longer add voice; the wizard offers the "Read chat aloud" checkbox (sets the global setting). Locales en-US/es-ES, README.
- T4 Full tests, macOS build (-Werror), install (OBS closed), README check.

## Out of scope
- Connectors, overlay pages, new TTS engines.

## Constraints
- Tests: `cmake -S tests -B build_tests_brew -DCMAKE_PREFIX_PATH=$(brew --prefix qt) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build_tests_brew -j8 && ctest --test-dir build_tests_brew --output-on-failure`. Base: 6/6 suites (test-chat 71, test-tts 59, test-overlay 20, test-box-measure 9, test-manager 31, overlay-js 6).
- macOS build: `cmake --preset macos && cmake --build --preset macos`, 0 warnings. Windows/Linux voice engines must keep compiling (no macOS-only API in shared code).
- RED→GREEN for pure logic (migration, defaults, blocked-words merge, filter-hidden messages not read). No push. Conventional commits, no AI attribution.

## Delivery
- Branch `feat/voice-settings`, stacked on `fix/final-review` (bafcfa3). Forecast ~700 authored lines → one more PR in the existing chain.

## Tasks
- [x] T1 Global voice settings + migration + single blocked-words list. Route: delegated (writer; 2+ non-trivial files). Commit fb7788a.
  - New `src/tts/voice-settings.{hpp,cpp}` (Qt Core only): `VoiceSettings` (JSON "voice" in config.json, off by default, clamped), `migrateVoiceSources`, `mergeBlockedWords`, `voiceSourceVersion`. `ChatManager::voiceSettings/setVoiceSettings/migrateLegacyVoice/voiceOutputState/setVoiceOutputState` + `voiceSettingsChanged`. Voice-source `blocked_words` property removed; `voice_migrate_legacy_sources()` collects user-set values (`obs_data_get_json`) of every `multichat_voice*` source after a collection loads.
  - RED: tests added first (8 in test-tts, 4 in test-manager); `cmake -S tests` failed (`Cannot find source file: src/tts/voice-settings.cpp`), the API did not exist. GREEN: test-tts 67/67, test-manager 35/35, all 6 suites pass; macOS build BUILD SUCCEEDED, 0 warnings.
  - "Filter-hidden messages are not read" is covered by `messagesHiddenByFiltersNeverReachTheVoice` (the voice only consumes `ChatManager::message`). The OBS enumeration part has no automated test.
- [x] T2 Plugin-owned voice output on a global channel, Monitor and Output, legacy sources silent. Route: delegated (writer). Commit b5bbe14.
  - Verified in `.deps/obs-studio-33.0.0`: `MAX_CHANNELS` = 64 (libobs/obs-defs.h); the frontend uses channel 0 (transition) and 1–6 (desktop/aux audio) and clears all 64 in `ClearSceneData` before `SCENE_COLLECTION_CLEANUP`; obs-audio.c mixes every channel; the mixer lists any public audio-active source; the frontend saves every public non-scene source into the collection (so the output is adopted from the collection on load, marked by the setting `multichat_output`). OBS 33 deprecates `obs_source_set_monitoring_type`: `obs_source_set_monitoring_enabled(src, true)` is Monitor and Output.
  - Output bound to the highest free channel 63→7 (`obs_get_output_source` checked, taken channels logged and skipped). Created/adopted when the voice is on (FINISHED_LOADING, SCENE_COLLECTION_CHANGED, settings change), removed when off; released (state saved, channel freed, ref dropped) on SCENE_COLLECTION_CHANGING/CLEANUP and module shutdown. Audio state (`obs_save_source`) saved in config.json "voiceOutput" on every frontend save and on release; restored with `obs_load_source` + `obs_source_load2` (fresh uuid). Both voice versions `OBS_SOURCE_CAP_OBSOLETE`; old sources never get an engine and their properties only show the note + "Open Multichat settings…".
  - No automated test (OBS runtime only); macOS build 0 warnings; 6/6 suites pass. Per-source voice locale keys removed here (unused until T3 re-added them for the tab).
- [ ] T3 Voice tab, wizard, Add sources, locales, README. Route: delegated (writer).
- [ ] T4 Tests, build, install. Route: delegated (writer) + parent spot check.

## Next step
T1.
