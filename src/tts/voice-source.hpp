#pragma once

// Fuente de audio "Multichat (voz)": lee el chat en voz alta en los tres sistemas.

void register_voice_source();
// Detiene las voces y deja de escuchar el chat. Se puede llamar más de una vez.
void voice_source_shutdown();
