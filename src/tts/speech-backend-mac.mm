// Voz en macOS con AVSpeechSynthesizer: writeUtterance entrega el audio en búferes en lugar de
// reproducirlo por los altavoces, así puede llegar a OBS como cualquier otra fuente de audio.

#include "speech-backend.hpp"

#import <AVFoundation/AVFoundation.h>

#include <algorithm>
#include <atomic>
#include <utility>

#if !__has_feature(objc_arc)
#error "Este archivo necesita ARC: Job guarda el sintetizador con una referencia fuerte."
#endif

namespace tts {

namespace {

// Un sintetizador por texto: así lo que quede de un texto cortado no se mezcla con el siguiente.
// El bloque del sintetizador retiene al Job y el Job al sintetizador; ese ciclo se rompe con
// synth = nil al terminar o al cancelar, y solo en la cola principal.
struct Job {
    SpeechBackend::PcmCallback onPcm;
    SpeechBackend::DoneCallback onDone;
    std::atomic<bool> cancelled {false};
    std::atomic<bool> finished {false};
    AVSpeechSynthesizer *synth = nil;
};

template<typename T>
PcmBuffer fromBufferList(const AudioBufferList *list, std::size_t frames, uint32_t channels, bool interleaved,
                         uint32_t sampleRate)
{
    const std::size_t planeCount = list->mNumberBuffers;
    std::vector<const T *> planes(planeCount);
    const std::size_t needed = frames * (interleaved ? channels : 1) * sizeof(T);
    for (std::size_t i = 0; i < planeCount; ++i) {
        if (list->mBuffers[i].mDataByteSize < needed)
            return {};
        planes[i] = static_cast<const T *>(list->mBuffers[i].mData);
    }
    return toFloatPcm<T>(planes.data(), planeCount, frames, channels, interleaved, sampleRate);
}

PcmBuffer convert(AVAudioPCMBuffer *buffer)
{
    AVAudioFormat *format = buffer.format;
    const AudioBufferList *list = buffer.audioBufferList;
    const std::size_t frames = buffer.frameLength;
    const uint32_t channels = format.channelCount;
    const bool interleaved = format.isInterleaved;
    const auto sampleRate = static_cast<uint32_t>(format.sampleRate + 0.5);

    switch (format.commonFormat) {
        case AVAudioPCMFormatFloat32:
            return fromBufferList<float>(list, frames, channels, interleaved, sampleRate);
        case AVAudioPCMFormatFloat64:
            return fromBufferList<double>(list, frames, channels, interleaved, sampleRate);
        case AVAudioPCMFormatInt16:
            return fromBufferList<int16_t>(list, frames, channels, interleaved, sampleRate);
        case AVAudioPCMFormatInt32:
            return fromBufferList<int32_t>(list, frames, channels, interleaved, sampleRate);
        default:
            return {};
    }
}

// Por valor a propósito: un bloque que captura una referencia de C++ no copia el objeto, y este
// bloque corre cuando el sintetizador ya puede haber soltado la copia del Job de la que venía.
void releaseSynthesizer(std::shared_ptr<Job> job)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        job->synth = nil;
    });
}

// También por valor: mantiene vivo el Job durante toda la llamada, aunque la cola principal
// suelte el sintetizador, y con él el bloque que nos llama, mientras se procesa este búfer.
void handleBuffer(std::shared_ptr<Job> job, AVAudioBuffer *buffer)
{
    if (job->cancelled.load() || ![buffer isKindOfClass:[AVAudioPCMBuffer class]])
        return;
    auto *pcm = static_cast<AVAudioPCMBuffer *>(buffer);
    // Un búfer vacío marca el final del texto.
    if (pcm.frameLength == 0) {
        if (!job->finished.exchange(true)) {
            job->onDone();
            releaseSynthesizer(job);
        }
        return;
    }
    PcmBuffer converted = convert(pcm);
    if (!converted.samples.empty())
        job->onPcm(std::move(converted));
}

class MacSpeechBackend : public SpeechBackend {
public:
    ~MacSpeechBackend() override { stop(); }

    void speak(const SpeechRequest &request, PcmCallback onPcm, DoneCallback onDone) override
    {
        stop();
        auto job = std::make_shared<Job>();
        job->onPcm = std::move(onPcm);
        job->onDone = std::move(onDone);
        current_ = job;

        const QByteArray text = request.text.toUtf8();
        const QByteArray voiceId = request.voiceId.toUtf8();
        NSString *nsText = [NSString stringWithUTF8String:text.constData()];
        NSString *nsVoice = voiceId.isEmpty() ? nil : [NSString stringWithUTF8String:voiceId.constData()];
        const double rate = std::clamp(request.rate, 0.0, 1.0);

        // Los bloques se quedan con su propia referencia al Job, no con este objeto, y nadie
        // espera a la cola principal: destruir el motor desde ella no puede bloquearse.
        dispatch_async(dispatch_get_main_queue(), ^{
            if (job->cancelled.load() || !nsText)
                return;
            AVSpeechUtterance *utterance = [AVSpeechUtterance speechUtteranceWithString:nsText];
            // Una voz que ya no existe queda en nil, que es la predeterminada del sistema.
            if (nsVoice)
                utterance.voice = [AVSpeechSynthesisVoice voiceWithIdentifier:nsVoice];
            utterance.rate = static_cast<float>(AVSpeechUtteranceMinimumSpeechRate +
                                                rate * (AVSpeechUtteranceMaximumSpeechRate -
                                                        AVSpeechUtteranceMinimumSpeechRate));
            job->synth = [[AVSpeechSynthesizer alloc] init];
            [job->synth writeUtterance:utterance
                      toBufferCallback:^(AVAudioBuffer *buffer) {
                          handleBuffer(job, buffer);
                      }];
        });
    }

    void stop() override
    {
        std::shared_ptr<Job> job = std::move(current_);
        current_.reset();
        if (!job)
            return;
        job->cancelled.store(true);
        dispatch_async(dispatch_get_main_queue(), ^{
            [job->synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
            job->synth = nil;
        });
    }

private:
    std::shared_ptr<Job> current_;
};

} // namespace

std::unique_ptr<SpeechBackend> createSpeechBackend()
{
    return std::make_unique<MacSpeechBackend>();
}

std::vector<VoiceInfo> availableVoices()
{
    std::vector<VoiceInfo> voices;
    for (AVSpeechSynthesisVoice *voice in [AVSpeechSynthesisVoice speechVoices]) {
        VoiceInfo info;
        info.id = QString::fromUtf8(voice.identifier.UTF8String);
        info.name = QStringLiteral("%1 (%2)").arg(QString::fromUtf8(voice.name.UTF8String),
                                                  QString::fromUtf8(voice.language.UTF8String));
        voices.push_back(std::move(info));
    }
    std::sort(voices.begin(), voices.end(), [](const VoiceInfo &a, const VoiceInfo &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return voices;
}

} // namespace tts
