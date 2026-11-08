#pragma once

// Fuente de audio "Multichat (voz)": lee el chat en voz alta en los tres sistemas.

#include "voice-settings.hpp" // kVoiceSourceId, kVoiceSourceVersion

void register_voice_source();
// Hands the settings of the voice sources saved in the loaded scenes to ChatManager, which keeps
// them only while config.json has no voice settings yet.
void voice_migrate_legacy_sources();

// The plugin's voice output ("Multichat (voice)"): one source on a free output channel, so it
// sounds in every scene and shows in the audio mixer, present only while the voice is on. All
// of these run on the Qt main thread.
// A scene collection finished loading: migrate old sources, then adopt or create the output.
void voice_collection_loaded();
// OBS is about to clear the scene collection (switch or exit): save the output's audio state and
// release its channel and reference so OBS can destroy it.
void voice_collection_unloading();
// Saves the output's volume, mute, monitoring and filters in config.json (OBS is saving).
void voice_save_state();
// Cuts the message being read.
void voice_skip_current();
// Detiene las voces y deja de escuchar el chat. Se puede llamar más de una vez.
void voice_source_shutdown();
