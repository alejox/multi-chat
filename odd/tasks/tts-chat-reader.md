# Feature: tts-chat-reader

## Objective
Read incoming chat comments aloud through a new OBS audio source, starting with macOS.

## Problem and why
Streamers miss comments while looking away from the chat. The voice must reach the stream, not only the local speakers, so it has to be an OBS audio source controllable from the mixer.

## Scope
- Platform-independent text filtering and bounded queue, with tests.
- macOS speech backend (AVSpeechSynthesizer rendered to PCM buffers).
- New input source "Multichat (voice)" with its options in the source properties, off by default.

## Out of scope
- Replying in chat or any platform login.
- Windows and Linux speech: the source is not registered there yet.
- Cloud voices.

## Constraints
- `ChatManager::message` is the only input; history must not be read aloud.
- Existing comments in the repository are in Spanish: extend in neutral Spanish. Identifiers stay in English.
- Install step and in-OBS listening test belong to the user with OBS closed.

## Delivery
- Forecast: about 800 authored changed lines.
- Strategy: `single-pr` (solo repository; commits stay as reviewable work units). Push and PR remain user decisions.
- RDD: off (decided by global), so no native review; ordinary checks apply.

## Tasks
- [x] T1 Text filter and queue with tests. Route: delegated (writer trigger, 2+ non-trivial files).
  - Accept: author prefix optional; links and emojis removed; emote parts skipped; long text truncated; blocked words reject the message; platform filter; empty results are not spoken; queue drops the oldest when full.
  - Check: `ctest --test-dir build_tests` with RED observed first.
- [x] T2 macOS backend and audio source. Route: delegated.
  - Accept: source registered only on macOS; PCM pushed at real-time pace with `obs_source_output_audio`; voice and rate are selectable; off by default; skip button; shutdown is clean.
  - Check: `cmake --build --preset macos` succeeds. Audible result needs a manual test in OBS.
- [x] T3 Locale strings and README. Route: delegated.
  - Accept: en-US and es-ES keys present for all properties; README documents usage and the macOS-only limit.
  - Check: every new key resolved in both locale files; `cmake --build --preset macos` succeeds.

## Progress
T1 done (13446ee, 5cf5086). T2 done (2c57922). T3 done (the commit that holds this line).

## Verification evidence
- Baseline `cmake --build --preset macos` before any change: BUILD SUCCEEDED (Xcode at /Applications/Xcode.app).
- `cmake -S tests -B build_tests -DCMAKE_PREFIX_PATH=<.deps Qt>`: fails at configure. The Qt in `.deps` has no QtTest module, and installing another Qt was denied by the permission system in this session. Tests ran against a local stand-in for the QtTest macros, kept outside the repository (`-DQt6Test_DIR=<scratchpad>`). CI on Ubuntu still runs them with the real QtTest; that run is pending.
- T1 RED with stub `speech-text.cpp`: test-tts 5 passed, 19 failed; test-chat passed.
- T1 GREEN: `ctest --test-dir build_tests`: test-chat and test-tts passed, 100%.
- T2 RED with stub `voice-engine.cpp`: test-tts 29 passed, 11 failed. The PCM conversion tests were written after `pcm.hpp`, so they have no RED.
- T2 GREEN: test-tts 40 passed, 0 failed, three runs in a row; also 40 passed with no reports under ThreadSanitizer and AddressSanitizer (scratch builds).
- T2 real synthesizer through a scratch harness, without OBS (`speech-backend-mac.mm` + `voice-engine.cpp`): 22050 Hz mono, peak 0.599, 3.56 s of audio delivered over 3.55 s; skip cut at 1.02 s and the next text played; destroying mid-utterance exited cleanly; no stalls. First audio took 3.2 s on one run and 0.56 s on another; about 0.45 s of silence between texts.
- T2 `cmake --build --preset macos`: BUILD SUCCEEDED; x86_64 and arm64; links AVFoundation.
- T3 keys: all 15 `Voice.*` keys used in `voice-source.cpp` exist in both locale files, with no extras; the 15 are in the built plugin's `es-ES.ini`. `cmake --build --preset macos`: BUILD SUCCEEDED. `build_tests/` and `build_macos/` are ignored by git.
- Not verified: listening in OBS, the mixer, `obs_source_output_audio`, source destruction and module unload inside OBS, Windows and Linux builds.

## Engram mirror
Pending: `mem_save` was blocked by the auto mode hook check earlier in this session. Topic: `odd/tts-chat-reader/tasks`.

## Next step
User installs the plugin with OBS closed and tests it by ear in OBS. CI must confirm the tests with the real QtTest and the Windows and Linux builds.
