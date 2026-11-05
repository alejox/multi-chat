// Voz en Windows con SAPI: ISpVoice renderiza el texto a PCM en memoria (no a los altavoces), y
// ese PCM llega a OBS como cualquier otra fuente de audio. Sin sphelper.h: se usan los objetos
// COM directamente para que el archivo quede limpio con /W4 y /WX en el CI.
//
// Los fallos se informan con qWarning, que OBS manda a su log: aquí no se depende de libobs para
// poder ejecutar este archivo en tests/test-tts-win.cpp.

#include "speech-backend.hpp"

#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <sapi.h>
#include <wrl/client.h>

#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace tts {

namespace {

// Un trabajo por texto. El hilo de síntesis guarda su propia referencia al Job, así que puede
// seguir vivo después de stop() o de destruir este objeto sin tocarlo.
struct Job {
	SpeechBackend::PcmCallback onPcm;
	SpeechBackend::DoneCallback onDone;
	std::atomic<bool> cancelled {false};
	std::atomic<bool> finished {false};
};

// Inicializa COM solo si este hilo no lo tenía ya, y lo suelta al salir.
struct ComInit {
	bool uninit = false;
	explicit ComInit(DWORD mode) { uninit = CoInitializeEx(nullptr, mode) == S_OK; }
	~ComInit()
	{
		if (uninit)
			CoUninitialize();
	}
};

// Formato que se pide a SAPI y con el que se interpretan los bytes que escribe.
constexpr uint32_t kSampleRate = 22050;
constexpr WORD kBitsPerSample = 16;

bool checked(HRESULT hr, const char *step)
{
	if (SUCCEEDED(hr))
		return true;
	qWarning("[multichat] voz de Windows: falló %s (HRESULT 0x%08lX)", step, static_cast<unsigned long>(hr));
	return false;
}

struct VoiceToken {
	QString id;
	QString name;
};

std::vector<VoiceToken> listVoiceTokens()
{
	std::vector<VoiceToken> voices;
	ComPtr<ISpObjectTokenCategory> category;
	if (!checked(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_ISpObjectTokenCategory,
				      &category),
		     "crear la categoría de voces") ||
	    !checked(category->SetId(SPCAT_VOICES, FALSE), "abrir la categoría de voces"))
		return voices;
	ComPtr<IEnumSpObjectTokens> enumerator;
	if (!checked(category->EnumTokens(nullptr, nullptr, &enumerator), "enumerar las voces"))
		return voices;
	ComPtr<ISpObjectToken> token;
	while (enumerator->Next(1, &token, nullptr) == S_OK) {
		VoiceToken entry;
		LPWSTR id = nullptr;
		if (SUCCEEDED(token->GetId(&id)) && id) {
			entry.id = QString::fromWCharArray(id);
			CoTaskMemFree(id);
		}
		LPWSTR name = nullptr;
		if (SUCCEEDED(token->GetStringValue(nullptr, &name)) && name) {
			entry.name = QString::fromWCharArray(name);
			CoTaskMemFree(name);
		}
		if (entry.name.isEmpty())
			entry.name = entry.id;
		if (!entry.id.isEmpty())
			voices.push_back(std::move(entry));
		token.Reset();
	}
	std::sort(voices.begin(), voices.end(), [](const VoiceToken &a, const VoiceToken &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return voices;
}

ComPtr<ISpObjectToken> findToken(const QString &id)
{
	const std::wstring wanted = id.toStdWString();
	ComPtr<ISpObjectTokenCategory> category;
	if (!checked(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_ISpObjectTokenCategory,
				      &category),
		     "crear la categoría de voces") ||
	    !checked(category->SetId(SPCAT_VOICES, FALSE), "abrir la categoría de voces"))
		return {};
	ComPtr<IEnumSpObjectTokens> enumerator;
	if (!checked(category->EnumTokens(nullptr, nullptr, &enumerator), "enumerar las voces"))
		return {};
	ComPtr<ISpObjectToken> token;
	while (enumerator->Next(1, &token, nullptr) == S_OK) {
		LPWSTR id = nullptr;
		const bool match = SUCCEEDED(token->GetId(&id)) && id && wanted == id;
		if (id)
			CoTaskMemFree(id);
		if (match)
			return token;
		token.Reset();
	}
	return {};
}

// Copia todo lo que SAPI escribió: PCM crudo, sin cabecera RIFF, en el formato de kSampleRate.
PcmBuffer readPcm(IStream *stream)
{
	STATSTG stat {};
	if (!checked(stream->Stat(&stat, STATFLAG_NONAME), "medir el audio generado"))
		return {};
	std::vector<int16_t> samples(static_cast<std::size_t>(stat.cbSize.QuadPart) / sizeof(int16_t));
	if (samples.empty())
		return {};
	LARGE_INTEGER zero {};
	if (!checked(stream->Seek(zero, STREAM_SEEK_SET, nullptr), "volver al inicio del audio"))
		return {};
	ULONG read = 0;
	if (!checked(stream->Read(samples.data(), static_cast<ULONG>(samples.size() * sizeof(int16_t)), &read),
		     "leer el audio generado"))
		return {};
	const int16_t *plane = samples.data();
	return toFloatPcm<int16_t>(&plane, 1, read / sizeof(int16_t), 1, true, kSampleRate);
}

// Corre en el hilo del trabajo, con COM ya inicializado. Devuelve un búfer vacío si algo falla,
// y cada fallo queda en el log.
PcmBuffer synthesize(const QString &text, const QString &voiceId, double rate)
{
	ComPtr<ISpVoice> voice;
	if (!checked(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, &voice), "crear ISpVoice"))
		return {};
	if (!voiceId.isEmpty()) {
		// Sin la voz elegida se sigue con la predeterminada: mejor esa que el silencio.
		const ComPtr<ISpObjectToken> token = findToken(voiceId);
		if (!token)
			qWarning("[multichat] voz de Windows: no existe la voz %s; se usa la predeterminada",
				 qUtf8Printable(voiceId));
		else
			checked(voice->SetVoice(token.Get()), "elegir la voz");
	}
	// rate 0..1 (0.5 normal) → ajuste SAPI de -10 (lento) a +10 (rápido).
	checked(voice->SetRate(std::lround((rate - 0.5) * 20.0)), "ajustar la velocidad");

	ComPtr<IStream> memStream;
	if (!checked(CreateStreamOnHGlobal(nullptr, TRUE, &memStream), "crear el búfer de audio"))
		return {};
	ComPtr<ISpStream> spStream;
	if (!checked(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, IID_ISpStream, &spStream), "crear ISpStream"))
		return {};

	WAVEFORMATEX format {};
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 1;
	format.nSamplesPerSec = kSampleRate;
	format.wBitsPerSample = kBitsPerSample;
	format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
	format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
	if (!checked(spStream->SetBaseStream(memStream.Get(), SPDFID_WaveFormatEx, &format), "preparar el audio"))
		return {};

	// FALSE: la voz debe convertir a este formato, porque es con el que se leen los bytes.
	if (!checked(voice->SetOutput(spStream.Get(), FALSE), "dirigir la voz al búfer"))
		return {};
	const std::wstring wtext = text.toStdWString();
	const bool spoken = checked(voice->Speak(wtext.c_str(), SPF_IS_NOT_XML | SPF_PURGEBEFORESPEAK, nullptr),
				    "sintetizar el texto") &&
			    checked(voice->WaitUntilDone(INFINITE), "esperar a la síntesis");
	// No se llama a SetOutput(nullptr): en el CI devolvía 0x8004503A, y no hace falta porque el búfer
	// ya está completo y la voz se suelta al salir.
	return spoken ? readPcm(memStream.Get()) : PcmBuffer {};
}

class WinSpeechBackend : public SpeechBackend {
public:
	~WinSpeechBackend() override { stop(); }

	void speak(const SpeechRequest &request, PcmCallback onPcm, DoneCallback onDone) override
	{
		stop();
		auto job = std::make_shared<Job>();
		job->onPcm = std::move(onPcm);
		job->onDone = std::move(onDone);
		current_ = job;

		const QString text = request.text;
		const QString voiceId = request.voiceId;
		const double rate = std::clamp(request.rate, 0.0, 1.0);

		// speak() y stop() corren en el hilo del motor. La síntesis va en un hilo propio para no
		// bloquearlo y para que stop() (saltar texto o apagar) no espere a que termine SAPI.
		std::thread([job, text, voiceId, rate] {
			ComInit com(COINIT_APARTMENTTHREADED);
			PcmBuffer buffer = synthesize(text, voiceId, rate);
			if (!job->cancelled.load() && !job->finished.exchange(true)) {
				if (buffer.samples.empty())
					qWarning("[multichat] voz de Windows: un texto terminó sin audio");
				else
					job->onPcm(std::move(buffer));
				job->onDone();
			}
		}).detach();
	}

	void stop() override
	{
		std::shared_ptr<Job> job = std::move(current_);
		current_.reset();
		if (job)
			job->cancelled.store(true);
	}

private:
	std::shared_ptr<Job> current_;
};

} // namespace

std::unique_ptr<SpeechBackend> createSpeechBackend()
{
	return std::make_unique<WinSpeechBackend>();
}

std::vector<VoiceInfo> availableVoices()
{
	ComInit com(COINIT_APARTMENTTHREADED);
	std::vector<VoiceInfo> voices;
	for (const VoiceToken &token : listVoiceTokens()) {
		VoiceInfo info;
		info.id = token.id;
		info.name = token.name;
		voices.push_back(std::move(info));
	}
	return voices;
}

} // namespace tts
