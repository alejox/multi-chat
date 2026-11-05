#pragma once

// Lee en orden una cola de textos: pide el audio al sintetizador, que va más rápido que el
// tiempo real, y lo entrega a trozos pequeños y a ritmo real desde un hilo propio.
// No sabe nada de OBS: recibe la salida y el reloj desde fuera, y por eso se puede probar.

#include "speech-backend.hpp"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace tts {

struct AudioChunk {
	const float *samples = nullptr; // intercalado
	uint32_t frames = 0;
	uint32_t channels = 0;
	uint32_t sampleRate = 0;
	uint64_t timestampNs = 0;
};

struct VoiceEngineConfig {
	// El sintetizador se crea con el primer texto, nunca antes.
	std::function<std::unique_ptr<SpeechBackend>()> backendFactory;
	// Se llama solo desde el hilo del motor, y nunca después de que termine el destructor.
	std::function<void(const AudioChunk &)> sink;
	std::function<uint64_t()> clockNs;
	// Aviso cuando el sintetizador deja de responder (también desde el hilo del motor).
	std::function<void()> onStall;
	std::size_t queueLimit = 20;
	uint32_t chunkMs = 20;
	uint32_t gapMs = 250;       // silencio entre dos textos
	uint32_t stallMs = 15000;   // sin audio durante este tiempo, se pasa al siguiente texto
	uint32_t resyncMs = 200;    // si el audio llega con este retraso, se sigue desde ahora
};

class VoiceEngine {
public:
	explicit VoiceEngine(VoiceEngineConfig config);
	// Detiene la voz y espera al hilo. Es seguro desde cualquier hilo salvo el propio motor,
	// y no depende de ningún otro hilo para terminar.
	~VoiceEngine();

	VoiceEngine(const VoiceEngine &) = delete;
	VoiceEngine &operator=(const VoiceEngine &) = delete;

	// Devuelve cuántos textos antiguos se descartaron para hacer sitio.
	std::size_t enqueue(const QString &text);
	void setVoice(const QString &voiceId, double rate);
	// Corta el texto actual y descarta el audio que le quedaba; sigue con el siguiente.
	void skipCurrent();
	// Vacía la cola y corta el texto actual.
	void clear();
	std::size_t droppedTotal() const;

private:
	struct Shared;
	void run();

	VoiceEngineConfig config_;
	std::shared_ptr<Shared> shared_;
	std::thread thread_;
};

} // namespace tts
