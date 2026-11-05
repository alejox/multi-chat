// Voz en Windows con SAPI: ISpVoice renderiza el texto a un WAV en memoria (no a los altavoces),
// y ese PCM llega a OBS como cualquier otra fuente de audio. Sin sphelper.h: se usan los objetos
// COM directamente para que el archivo quede limpio con /W4 y /WX en el CI.

#include "speech-backend.hpp"
#include "wav.hpp"

#include <sapi.h>
#include <wrl/client.h>

#include <QString>

#include <algorithm>
#include <atomic>
#include <cmath>
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

struct VoiceToken {
	QString id;
	QString name;
};

std::vector<VoiceToken> listVoiceTokens()
{
	std::vector<VoiceToken> voices;
	ComPtr<ISpObjectTokenCategory> category;
	if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
				    IID_ISpObjectTokenCategory, &category)) ||
	    FAILED(category->SetId(SPCAT_VOICES, FALSE)))
		return voices;
	ComPtr<IEnumSpObjectTokens> enumerator;
	if (FAILED(category->EnumTokens(nullptr, nullptr, &enumerator)))
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
	if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
				    IID_ISpObjectTokenCategory, &category)) ||
	    FAILED(category->SetId(SPCAT_VOICES, FALSE)))
		return {};
	ComPtr<IEnumSpObjectTokens> enumerator;
	if (FAILED(category->EnumTokens(nullptr, nullptr, &enumerator)))
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
			PcmBuffer buffer;
			ComPtr<ISpVoice> voice;
			if (SUCCEEDED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, &voice))) {
				if (!voiceId.isEmpty()) {
					const ComPtr<ISpObjectToken> token = findToken(voiceId);
					if (token)
						voice->SetVoice(token.Get());
				}
				// rate 0..1 (0.5 normal) → ajuste SAPI de -10 (lento) a +10 (rápido).
				voice->SetRate(std::lround((rate - 0.5) * 20.0));

				ComPtr<IStream> memStream;
				if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &memStream))) {
					ComPtr<ISpStream> spStream;
					if (SUCCEEDED(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, IID_ISpStream,
								       &spStream)) &&
					    SUCCEEDED(spStream->SetBaseStream(memStream.Get(), SPDFID_WaveFormatEx, nullptr))) {
						voice->SetOutput(spStream.Get(), TRUE);
						const std::wstring wtext = text.toStdWString();
						voice->Speak(wtext.c_str(), SPF_IS_NOT_XML | SPF_PURGEBEFORESPEAK, nullptr);
						voice->WaitUntilDone(INFINITE);
						voice->SetOutput(nullptr, TRUE);

						LARGE_INTEGER zero {};
						if (SUCCEEDED(memStream->Seek(zero, STREAM_SEEK_SET, nullptr))) {
							STATSTG stat {};
							if (SUCCEEDED(memStream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > 0) {
								std::vector<uint8_t> wav(static_cast<std::size_t>(stat.cbSize.QuadPart));
								ULONG read = 0;
								if (SUCCEEDED(memStream->Read(wav.data(), static_cast<ULONG>(wav.size()), &read)))
									buffer = fromWav(wav.data(), read);
							}
						}
					}
				}
			}
			if (!job->cancelled.load() && !job->finished.exchange(true)) {
				if (!buffer.samples.empty())
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
