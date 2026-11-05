// Voz en Linux con espeak-ng: la síntesis va a un búfer propio vía callback (AUDIO_OUTPUT_RETRIEVAL)
// en lugar de a los altavoces, y ese PCM llega a OBS como cualquier otra fuente de audio.

#include "speech-backend.hpp"

#include <espeak-ng/speak_lib.h>

#include <QString>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace tts {

namespace {

// espeak-ng no es reentrante: un candado global serializa todo su uso (síntesis y listado de
// voces), que además ocurre en hilos distintos (motor y diálogo de propiedades).
std::mutex g_espeakMutex;
bool g_espeakTried = false;
int g_espeakSampleRate = 0;
std::vector<int16_t> g_capture;

// El callback de espeak-ng es una función C global, así que deja las muestras en g_capture,
// que solo se toca con el candado tomado.
int captureCallback(short *wav, int numsamples, espeak_EVENT * /*events*/)
{
	if (wav && numsamples > 0)
		g_capture.insert(g_capture.end(), wav, wav + numsamples);
	return 0; // seguir sintetizando
}

// Inicializa espeak-ng una sola vez para todo el proceso. Devuelve la frecuencia o 0 si falla.
int ensureEspeak()
{
	std::lock_guard<std::mutex> lock(g_espeakMutex);
	if (g_espeakTried)
		return g_espeakSampleRate;
	g_espeakTried = true;
	g_espeakSampleRate = espeak_Initialize(AUDIO_OUTPUT_RETRIEVAL, 0, nullptr, 0);
	if (g_espeakSampleRate > 0)
		espeak_SetSynthCallback(captureCallback);
	return g_espeakSampleRate;
}

class EspeakBackend : public SpeechBackend {
public:
	void speak(const SpeechRequest &request, PcmCallback onPcm, DoneCallback onDone) override
	{
		const int sampleRate = ensureEspeak();
		if (sampleRate <= 0) {
			onDone(); // sin voz: el texto se pierde, igual que hace el motor sin backend
			return;
		}

		PcmBuffer buffer;
		{
			std::lock_guard<std::mutex> lock(g_espeakMutex);
			const QByteArray voiceId = request.voiceId.toUtf8();
			if (!voiceId.isEmpty())
				espeak_SetVoiceByName(voiceId.constData());

			// rate 0..1 (0.5 normal) → palabras por minuto de espeak-ng, con 175 como velocidad normal.
			const double r = std::clamp(request.rate, 0.0, 1.0);
			const int wpm = r < 0.5 ? int(80.0 + r * 2.0 * (175.0 - 80.0))
						: int(175.0 + (r - 0.5) * 2.0 * (450.0 - 175.0));
			espeak_SetParameter(espeakRATE, wpm, 0);

			g_capture.clear();
			const QByteArray text = request.text.toUtf8();
			espeak_Synth(text.constData(), text.size() + 1, 0, POS_CHARACTER, 0, espeakCHARS_UTF8, nullptr,
				     nullptr);
			espeak_Synchronize();

			buffer.sampleRate = static_cast<uint32_t>(sampleRate);
			buffer.channels = 1;
			buffer.samples.reserve(g_capture.size());
			for (int16_t s : g_capture)
				buffer.samples.push_back(sampleToFloat(s));
		}

		if (!buffer.samples.empty())
			onPcm(std::move(buffer));
		onDone();
	}

	void stop() override
	{
		// Con AUDIO_OUTPUT_RETRIEVAL la síntesis es síncrona: no queda nada en curso que cortar.
	}
};

} // namespace

std::unique_ptr<SpeechBackend> createSpeechBackend()
{
	return std::make_unique<EspeakBackend>();
}

std::vector<VoiceInfo> availableVoices()
{
	std::vector<VoiceInfo> voices;
	if (ensureEspeak() <= 0)
		return voices;
	std::lock_guard<std::mutex> lock(g_espeakMutex);
	const espeak_VOICE **list = espeak_ListVoices(nullptr);
	for (int i = 0; list && list[i]; ++i) {
		if (!list[i]->identifier || !list[i]->name)
			continue;
		const QString identifier = QString::fromUtf8(list[i]->identifier);
		// Las voces mbrola (mb-*) piden datos externos que no están instalados: se omiten.
		if (identifier.isEmpty() || identifier.startsWith(QStringLiteral("mb")))
			continue;
		VoiceInfo info;
		info.id = identifier;
		info.name = QString::fromUtf8(list[i]->name);
		if (info.name.isEmpty())
			info.name = info.id;
		voices.push_back(std::move(info));
	}
	std::sort(voices.begin(), voices.end(), [](const VoiceInfo &a, const VoiceInfo &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return voices;
}

} // namespace tts
