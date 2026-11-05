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
- [x] T4 Corrections found after T2. Route: delegated (one writer; the fixes touch the backend, the engine, the filter and the tests).
  - Accept: no reference capture in a block that outlives its caller; each new text starts at the current time when behind, without timestamps regressing; blocked words ignore the literal text of the author format; strings on the engine thread are autoreleased inside a pool.
  - Check: `ctest --test-dir build_tests`; `cmake --build --preset macos`; the real backend scratch harness under AddressSanitizer.

## Progress
T1 done (13446ee, 5cf5086). T2 done (2c57922), reopened and closed by T4: the user's real OBS crashed twice with SIGSEGV at 0x48 at `speech-backend-mac.mm:72`, on the main thread. T3 done (60e8585). T4 done (51c6568, e2400de, 1c5db74, 317d81d; this document in the commit that holds this line).

T2 closes on the crash fix only as far as the evidence below reaches: its acceptance "shutdown is clean" and any stability inside OBS remain unverified until the user tests.

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
- T4 crash RED: scratch harness with the real `speech-backend-mac.mm` under AddressSanitizer. With the main thread free it did not reproduce (exit 0). With the main thread sleeping 250 ms between run-loop slices it reproduced 3 of 3 runs: heap-use-after-free in the `releaseSynthesizer` block at `speech-backend-mac.mm:72`, the memory freed by `_Block_release` of the `writeUtterance` callback block. `offsetof(Job, synth)` is 0x48 on arm64, which matches the crash address. The T2 harness passed because it never delayed the main thread.
- T4 crash GREEN (51c6568): same harness, 3 of 3 busy runs exit 0. On the final code (317d81d) 3 runs in free mode and 2 in busy mode exit 0, covering five texts to completion, a skip and a destroy mid-utterance, with no stalls and no timestamp regressions.
- T4 blocked words RED: test-tts 40 passed, 1 failed (`blockedWordsIgnoreTheAuthorFormat`). GREEN: e2400de.
- T4 timeline RED: test-tts 43 passed, 1 failed (`firstChunkOfATextStartsAtTheCurrentTime`), with the fake backend. GREEN: 1c5db74. With the real synthesizer the lag was not reproduced: over 8 skips with the old engine the gap to the first chunk was 228-261 ms, above the resync threshold, so the old code already resynced. The defect is proven against the fake backend only.
- T4 autorelease RED: with `OBJC_DEBUG_MISSING_POOLS=YES`, 24 reports on the engine thread at -O0 (`__NSCFString` among them) and 0 at -O1, so optimized builds did not show it. GREEN (317d81d): 0 reports at -O0. The leak was not measured.
- T4 tests on 317d81d, again against the local QtTest stand-in: test-tts 44 passed, 0 failed; test-chat passed. `cmake --build --preset macos`: BUILD SUCCEEDED; x86_64 and arm64.
- Not verified: listening in OBS, the mixer, `obs_source_output_audio`, the effect on OBS audio buffering, source destruction and module unload inside OBS, Windows and Linux builds. No build containing T4 has been installed or run in OBS, so the crash fix is unproven there. A block still queued on the main queue at module unload was not tested.

## Engram mirror
Pending: `mem_save` was blocked by the auto mode hook check earlier in this session. Topic: `odd/tts-chat-reader/tasks`.

## Next step
User installs the plugin built with T4 with OBS closed and tests it by ear in OBS, including several texts to completion, skip and closing OBS. CI must confirm the tests with the real QtTest and the Windows and Linux builds.
