#pragma once

// Fuente de audio "Multichat (voz)": lee el chat en voz alta en los tres sistemas.

#include "voice-settings.hpp" // kVoiceSourceId, kVoiceSourceVersion

void register_voice_source();
// Hands the settings of the voice sources saved in the loaded scenes to ChatManager, which keeps
// them only while config.json has no voice settings yet. Main thread, after a collection loads.
void voice_migrate_legacy_sources();
// Detiene las voces y deja de escuchar el chat. Se puede llamar más de una vez.
void voice_source_shutdown();
