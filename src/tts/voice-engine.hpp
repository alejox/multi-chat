#pragma once

// Lee en orden una cola de textos: pide el audio al sintetizador, que va más rápido que el
// tiempo real, y lo entrega a trozos pequeños y a ritmo real desde un hilo propio.
// No sabe nada de OBS: recibe la salida y el reloj desde fuera, y por eso se puede probar.

#include "speech-backend.hpp"

#include <QJsonObject>
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
	uint32_t resyncMs = 200;    // si dentro de un texto el audio llega con este retraso, se sigue desde ahora
};

class VoiceEngine {
public:
	explicit VoiceEngine(VoiceEngineConfig config);
	// Detiene la voz y espera al hilo. Es seguro desde cualquier hilo salvo el propio motor,
	// y no depende de ningún otro hilo para terminar.
	~VoiceEngine();

	VoiceEngine(const VoiceEngine &) = delete;
	VoiceEngine &operator=(const VoiceEngine &) = delete;

	// Devuelve cuántos elementos antiguos se descartaron para hacer sitio. `message` is the
	// identity of the chat message (source, platform, id, authorId, author) so cancel() can drop
	// it if a deletion matches it before it is read. `sound`, when given, plays right before the
	// text as part of the same item: a full queue or a deletion drops both together.
	std::size_t enqueue(const QString &text, const QJsonObject &message = QJsonObject(),
			    std::shared_ptr<const PcmBuffer> sound = nullptr);
	// Queues ready-made audio (the notification sound) alone, in the same order as the texts: no
	// synthesizer involved, same real-time pacing, and cancel() drops it like a text. The buffer
	// is shared, not copied. A null or empty buffer is ignored (returns 0).
	std::size_t enqueueSound(std::shared_ptr<const PcmBuffer> sound, const QJsonObject &message = QJsonObject());
	// Drops the queued items a deletion event matches (chat::deletionMatches) and cuts the
	// current one if it matches. Returns how many items it dropped or cut.
	std::size_t cancel(const QJsonObject &deletion);
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
