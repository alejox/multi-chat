#pragma once

// Notification sound of the "Multichat (voice)" source: a built-in chime synthesized here (no
// bundled asset), an optional WAV chosen by the user, and the cooldown that keeps a busy chat
// from ringing nonstop. Pure functions, without OBS: tests/test-tts.cpp covers them.

#include "pcm.hpp"

#include <QByteArray>

#include <cstdint>

namespace tts {

// Sample rate the sounds are delivered at (a user WAV is resampled to it).
constexpr uint32_t kChimeSampleRate = 48000;

// Two short sine tones with an attack/decay envelope, about 250 ms, peak at -12 dBFS, mono.
// Empty for a sample rate of 0.
PcmBuffer notificationChime(uint32_t sampleRate);

// Minimal, bounds-checked WAV reader: RIFF/WAVE with PCM 16-bit samples, mono or stereo, at
// most `maxSeconds` long. Anything else (other formats, truncated or oversized chunks) gives an
// empty buffer.
PcmBuffer parseWav(const QByteArray &data, double maxSeconds = 10.0);

// Linear-interpolation resampling to `sampleRate` (a copy when it already matches). Empty for
// an invalid buffer or a rate of 0.
PcmBuffer resamplePcm(const PcmBuffer &in, uint32_t sampleRate);

// Flood control: true when a sound may play now. lastChimeNs = 0 means none played yet; a
// cooldown of 0 allows one per message.
bool chimeAllowed(uint64_t nowNs, uint64_t lastChimeNs, int cooldownMs);

} // namespace tts
