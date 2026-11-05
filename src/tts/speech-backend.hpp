#pragma once

// Sintetizador de voz de cada sistema: speech-backend-mac.mm, speech-backend-win.cpp y
// speech-backend-linux.cpp. Para añadir otro basta con implementar esta interfaz, sin tocar la fuente de OBS.

#include "pcm.hpp"

#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace tts {

struct VoiceInfo {
	QString id;
	QString name; // nombre para mostrar
};

struct SpeechRequest {
	QString text;
	QString voiceId; // vacío = la voz predeterminada del sistema
	double rate = 0.5; // de 0 (lo más lento) a 1 (lo más rápido); 0.5 es la velocidad normal
};

class SpeechBackend {
public:
	using PcmCallback = std::function<void(PcmBuffer)>;
	using DoneCallback = std::function<void()>;

	virtual ~SpeechBackend() = default;

	// Empieza a generar el audio de un texto y vuelve enseguida; no debe bloquear.
	// Las funciones se llaman desde cualquier hilo y pueden llegar incluso después de stop()
	// o de destruir este objeto, así que no deben depender de él. El sintetizador puede no
	// avisar del final tras un stop(): quien llama no debe esperar a done.
	virtual void speak(const SpeechRequest &request, PcmCallback onPcm, DoneCallback onDone) = 0;

	// Cancela lo que se esté generando. Tampoco bloquea.
	virtual void stop() = 0;
};

// Definidas por cada sistema.
std::unique_ptr<SpeechBackend> createSpeechBackend();
std::vector<VoiceInfo> availableVoices();

} // namespace tts
