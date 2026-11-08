#pragma once

// Fuente de audio "Multichat (voz)": lee el chat en voz alta en los tres sistemas.

#include <cstdint>

namespace tts {

// The source is registered twice under the same id: version 0 keeps the scenes saved before it
// read aloud by default (hidden from "Add Source"), and this version is what new sources use
// (OBS names it "multichat_voice_v2").
constexpr const char *kVoiceSourceId = "multichat_voice";
constexpr uint32_t kVoiceSourceVersion = 2;

// OBS only saves the values the user changed, so a version 0 source that was never turned on
// has no "enabled" saved: its default must stay off or it would start reading the live chat.
constexpr bool voiceEnabledByDefault(uint32_t version)
{
	return version >= kVoiceSourceVersion;
}

} // namespace tts

void register_voice_source();
// Detiene las voces y deja de escuchar el chat. Se puede llamar más de una vez.
void voice_source_shutdown();
