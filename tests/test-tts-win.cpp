// Prueba del sintetizador real de Windows (SAPI) a través de createSpeechBackend(). Solo corre en
// Windows y no usa QtTest porque el Qt que descarga el job de Windows no lo trae.
//
// Código de salida: 0 todo bien, 1 algún fallo, 2 el sistema no tiene voces (no se probó nada).

#include "../src/tts/speech-backend.hpp"

#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace tts;

namespace {

constexpr int kExitPass = 0;
constexpr int kExitFail = 1;
constexpr int kExitNoVoices = 2;

constexpr std::chrono::seconds kTimeout {60};
constexpr std::chrono::seconds kCancelWait {5};
constexpr double kMinSeconds = 0.2; // menos que esto no es una frase hablada

const QString kSpeechText = QStringLiteral("Hola, esto es una prueba de voz.");

std::atomic<int> g_warnings {0};

// Sin consola, Qt puede mandar los avisos a la salida de depuración: aquí se imprimen y se cuentan.
void countingMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
	if (type != QtDebugMsg && type != QtInfoMsg)
		g_warnings.fetch_add(1);
	std::printf("  [qt] %s\n", qUtf8Printable(message));
}

// Sobrevive al backend: el hilo de síntesis puede llamar a las funciones más tarde.
struct Outcome {
	std::mutex mutex;
	std::condition_variable changed;
	bool done = false;
	int pcmCalls = 0;
	PcmBuffer pcm;
};

std::shared_ptr<Outcome> startSpeaking(SpeechBackend &backend, const SpeechRequest &request)
{
	auto outcome = std::make_shared<Outcome>();
	backend.speak(
		request,
		[outcome](PcmBuffer pcm) {
			std::lock_guard<std::mutex> lock(outcome->mutex);
			++outcome->pcmCalls;
			outcome->pcm = std::move(pcm);
		},
		[outcome] {
			std::lock_guard<std::mutex> lock(outcome->mutex);
			outcome->done = true;
			outcome->changed.notify_all();
		});
	return outcome;
}

bool fail(const char *reason)
{
	std::printf("  FALLO: %s\n", reason);
	return false;
}

bool waitForDone(Outcome &outcome, std::unique_lock<std::mutex> &lock)
{
	return outcome.changed.wait_for(lock, kTimeout, [&outcome] { return outcome.done; });
}

// Un texto debe terminar dentro del límite y entregar audio de verdad. Solo se admiten avisos
// cuando el caso los espera, y entonces son obligatorios.
bool expectAudio(const char *label, const SpeechRequest &request, bool expectWarning = false)
{
	std::printf("== %s\n", label);
	const int warningsBefore = g_warnings.load();
	auto backend = createSpeechBackend();
	auto outcome = startSpeaking(*backend, request);

	std::unique_lock<std::mutex> lock(outcome->mutex);
	if (!waitForDone(*outcome, lock))
		return fail("el sintetizador no terminó dentro del límite de tiempo");

	const PcmBuffer &pcm = outcome->pcm;
	float peak = 0.0f;
	for (const float sample : pcm.samples)
		peak = std::max(peak, std::fabs(sample));
	const double seconds = pcm.sampleRate ? double(pcm.frames()) / double(pcm.sampleRate) : 0.0;
	std::printf("  llamadas PCM=%d frecuencia=%u canales=%u muestras=%zu duracion=%.2fs pico=%.3f\n",
		    outcome->pcmCalls, pcm.sampleRate, pcm.channels, pcm.samples.size(), seconds, double(peak));

	if (outcome->pcmCalls == 0 || pcm.samples.empty())
		return fail("terminó sin entregar audio");
	if (pcm.sampleRate == 0 || pcm.channels == 0)
		return fail("el audio no trae formato");
	if (peak <= 0.0f)
		return fail("todas las muestras son cero");
	if (seconds < kMinSeconds)
		return fail("el audio es demasiado corto para ser la frase");

	const bool warned = g_warnings.load() != warningsBefore;
	if (warned && !expectWarning)
		return fail("hubo audio, pero el sintetizador avisó de un problema");
	if (!warned && expectWarning)
		return fail("el caso debía dejar un aviso en el log y no lo dejó");
	std::printf("  ok\n");
	return true;
}

// Un texto vacío no produce audio: el trabajo debe terminar igual y dejar rastro, nunca callar.
bool expectVisibleSilence()
{
	std::printf("== texto vacio: termina y avisa\n");
	const int warningsBefore = g_warnings.load();
	auto backend = createSpeechBackend();
	SpeechRequest request;
	auto outcome = startSpeaking(*backend, request);

	std::unique_lock<std::mutex> lock(outcome->mutex);
	if (!waitForDone(*outcome, lock))
		return fail("el sintetizador no terminó dentro del límite de tiempo");
	if (outcome->pcmCalls != 0)
		return fail("entregó audio de un texto vacío");
	if (g_warnings.load() == warningsBefore)
		return fail("terminó sin audio y sin ningún aviso en el log");
	std::printf("  ok\n");
	return true;
}

// Lo cancelado con stop() no debe llegar nunca, aunque SAPI termine después.
bool expectCancelledSilence()
{
	std::printf("== stop(): no entrega audio\n");
	auto backend = createSpeechBackend();
	SpeechRequest request;
	request.text = kSpeechText;
	auto outcome = startSpeaking(*backend, request);
	backend->stop();
	std::this_thread::sleep_for(kCancelWait);

	std::lock_guard<std::mutex> lock(outcome->mutex);
	if (outcome->pcmCalls != 0)
		return fail("entregó audio de un texto cancelado");
	std::printf("  ok\n");
	return true;
}

int runAll()
{
	const std::vector<VoiceInfo> voices = availableVoices();
	std::printf("voces en este sistema: %zu\n", voices.size());
	for (const VoiceInfo &voice : voices)
		std::printf("  - %s | %s\n", qUtf8Printable(voice.name), qUtf8Printable(voice.id));
	if (voices.empty()) {
		std::printf("SIN VOCES: este sistema no tiene voces SAPI, no se probo el sintetizador\n");
		return kExitNoVoices;
	}

	bool ok = true;
	SpeechRequest request;
	request.text = kSpeechText;
	ok &= expectAudio("voz predeterminada", request);

	for (const VoiceInfo &voice : voices) {
		request.voiceId = voice.id;
		const std::string label = "voz explicita: " + voice.name.toStdString();
		ok &= expectAudio(label.c_str(), request);
	}

	request.voiceId = QStringLiteral("voz-que-no-existe");
	ok &= expectAudio("voz inexistente: usa la predeterminada y avisa", request, true);

	ok &= expectVisibleSilence();
	ok &= expectCancelledSilence();

	std::printf(ok ? "RESULTADO: todo bien\n" : "RESULTADO: hubo fallos\n");
	return ok ? kExitPass : kExitFail;
}

} // namespace

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	qInstallMessageHandler(countingMessageHandler);

	const int code = runAll();
	// Sale sin destructores globales: un hilo de SAPI colgado no debe impedir terminar.
	std::fflush(nullptr);
	std::_Exit(code);
}
