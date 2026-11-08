#include "voice-engine.hpp"
#include "bounded-queue.hpp"
#include "../chat/message.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace tts {

namespace {
using SteadyClock = std::chrono::steady_clock;

struct QueuedText {
	QString text;                           // empty: only the sound
	QJsonObject message;                    // identity of the chat message, empty if unknown
	std::shared_ptr<const PcmBuffer> sound; // ready-made audio played before the text, if any
};

bool playable(const std::shared_ptr<const PcmBuffer> &sound)
{
	return sound && !sound->samples.empty() && sound->channels && sound->sampleRate;
}

constexpr uint64_t kNsPerSecond = 1000000000ULL;
constexpr uint64_t kNsPerMs = 1000000ULL;
} // namespace

// Estado compartido con las funciones que se le pasan al sintetizador. Esas funciones pueden
// llegar después de destruir el motor: por eso guardan su propia referencia a este objeto y
// nunca al motor. Cada texto tiene su generación, y lo que llegue de otra se ignora.
struct VoiceEngine::Shared {
	explicit Shared(std::size_t queueLimit) : queue(queueLimit) {}

	std::mutex mutex;
	std::condition_variable wake;
	BoundedQueue<QueuedText> queue;
	QJsonObject currentMessage; // identity of the item being played
	std::deque<std::shared_ptr<const PcmBuffer>> pcm;
	std::size_t pcmOffset = 0; // muestras ya entregadas del primer búfer
	uint64_t generation = 0;
	bool renderDone = false;
	bool skip = false;
	bool stop = false;
	QString voiceId;
	double rate = 0.5;
	SteadyClock::time_point lastActivity;
};

VoiceEngine::VoiceEngine(VoiceEngineConfig config)
	: config_(std::move(config)),
	  shared_(std::make_shared<Shared>(config_.queueLimit))
{
	thread_ = std::thread([this] { run(); });
}

VoiceEngine::~VoiceEngine()
{
	{
		std::lock_guard<std::mutex> lock(shared_->mutex);
		shared_->stop = true;
	}
	shared_->wake.notify_all();
	thread_.join();
}

std::size_t VoiceEngine::enqueue(const QString &text, const QJsonObject &message, std::shared_ptr<const PcmBuffer> sound)
{
	if (!playable(sound))
		sound.reset();
	std::size_t dropped;
	{
		std::lock_guard<std::mutex> lock(shared_->mutex);
		dropped = shared_->queue.push(QueuedText{text, message, std::move(sound)});
	}
	shared_->wake.notify_all();
	return dropped;
}

std::size_t VoiceEngine::enqueueSound(std::shared_ptr<const PcmBuffer> sound, const QJsonObject &message)
{
	if (!playable(sound))
		return 0;
	return enqueue(QString(), message, std::move(sound));
}

std::size_t VoiceEngine::cancel(const QJsonObject &deletion)
{
	if (!chat::isDeletion(deletion))
		return 0;
	const auto matches = [&deletion](const QJsonObject &message) {
		return !message.isEmpty() && chat::deletionMatches(deletion, message);
	};
	std::size_t cancelled;
	{
		std::lock_guard<std::mutex> lock(shared_->mutex);
		cancelled = shared_->queue.removeIf([&matches](const QueuedText &item) { return matches(item.message); });
		if (matches(shared_->currentMessage)) {
			shared_->currentMessage = QJsonObject();
			shared_->skip = true;
			++cancelled;
		}
	}
	shared_->wake.notify_all();
	return cancelled;
}

void VoiceEngine::setVoice(const QString &voiceId, double rate)
{
	std::lock_guard<std::mutex> lock(shared_->mutex);
	shared_->voiceId = voiceId;
	shared_->rate = rate;
}

void VoiceEngine::skipCurrent()
{
	{
		std::lock_guard<std::mutex> lock(shared_->mutex);
		shared_->skip = true;
	}
	shared_->wake.notify_all();
}

void VoiceEngine::clear()
{
	{
		std::lock_guard<std::mutex> lock(shared_->mutex);
		shared_->queue.clear();
		shared_->skip = true;
	}
	shared_->wake.notify_all();
}

std::size_t VoiceEngine::droppedTotal() const
{
	std::lock_guard<std::mutex> lock(shared_->mutex);
	return shared_->queue.droppedTotal();
}

void VoiceEngine::run()
{
	Shared &s = *shared_;
	std::unique_ptr<SpeechBackend> backend; // solo lo toca este hilo
	bool speaking = false;
	bool firstChunkOfText = false;
	uint64_t nextTimestamp = 0;
	std::vector<float> chunk;
	const auto stallTimeout = std::chrono::milliseconds(config_.stallMs);
	const auto interrupted = [&s] { return s.stop || s.skip; };

	std::unique_lock<std::mutex> lock(s.mutex);

	// Invalida lo que llegue del texto actual y cancela el sintetizador. Vuelve con el candado.
	const auto abandonCurrent = [&] {
		++s.generation;
		s.pcm.clear();
		s.pcmOffset = 0;
		s.renderDone = false;
		speaking = false;
		s.currentMessage = QJsonObject();
		lock.unlock();
		if (backend)
			backend->stop();
		lock.lock();
	};

	while (!s.stop) {
		if (s.skip) {
			s.skip = false;
			if (speaking)
				abandonCurrent();
			continue;
		}

		if (!speaking) {
			QueuedText next;
			if (!s.queue.pop(next)) {
				s.wake.wait(lock);
				continue;
			}
			const uint64_t generation = ++s.generation;
			s.currentMessage = next.message;
			speaking = true;
			firstChunkOfText = true;
			s.lastActivity = SteadyClock::now();
			// A sound is already rendered: it only needs the real-time delivery below, and the
			// text's audio (if any) is appended after it.
			if (next.sound)
				s.pcm.push_back(std::move(next.sound));
			if (next.text.isEmpty()) {
				s.renderDone = true;
				continue;
			}
			SpeechRequest request{next.text, s.voiceId, s.rate};
			s.renderDone = false;
			lock.unlock();

			if (!backend && config_.backendFactory)
				backend = config_.backendFactory();
			if (backend) {
				const std::shared_ptr<Shared> shared = shared_;
				backend->speak(
					request,
					[shared, generation](PcmBuffer buffer) {
						if (buffer.samples.empty() || !buffer.channels || !buffer.sampleRate)
							return;
						{
							std::lock_guard<std::mutex> guard(shared->mutex);
							if (generation != shared->generation)
								return;
							shared->pcm.push_back(std::make_shared<const PcmBuffer>(std::move(buffer)));
							shared->lastActivity = SteadyClock::now();
						}
						shared->wake.notify_all();
					},
					[shared, generation] {
						{
							std::lock_guard<std::mutex> guard(shared->mutex);
							if (generation != shared->generation)
								return;
							shared->renderDone = true;
						}
						shared->wake.notify_all();
					});
			}

			lock.lock();
			if (!backend) {
				// este sistema no tiene voz: el texto se pierde (el sonido, si lo hay, suena)
				if (s.pcm.empty()) {
					speaking = false;
					s.currentMessage = QJsonObject();
				} else {
					s.renderDone = true;
				}
			}
			continue;
		}

		if (s.pcm.empty()) {
			if (s.renderDone) {
				speaking = false;
				s.currentMessage = QJsonObject();
				s.renderDone = false;
				s.wake.wait_for(lock, std::chrono::milliseconds(config_.gapMs), interrupted);
			} else if (SteadyClock::now() - s.lastActivity >= stallTimeout) {
				abandonCurrent();
				lock.unlock();
				if (config_.onStall)
					config_.onStall();
				lock.lock();
			} else {
				s.wake.wait_until(lock, s.lastActivity + stallTimeout);
			}
			continue;
		}

		// Un trozo del primer búfer, a ritmo real.
		const PcmBuffer &front = *s.pcm.front();
		const uint32_t rate = front.sampleRate;
		const uint32_t channels = front.channels;
		const std::size_t chunkFrames = std::max<std::size_t>(1, std::size_t(rate) * config_.chunkMs / 1000);
		const std::size_t available = (front.samples.size() - s.pcmOffset) / channels;
		const std::size_t frames = std::min(chunkFrames, available);
		const auto begin = front.samples.begin() + static_cast<std::ptrdiff_t>(s.pcmOffset);
		chunk.assign(begin, begin + static_cast<std::ptrdiff_t>(frames * channels));
		s.pcmOffset += frames * channels;
		if (s.pcmOffset + channels > front.samples.size()) {
			s.pcm.pop_front();
			s.pcmOffset = 0;
		}
		s.lastActivity = SteadyClock::now();
		lock.unlock();

		const uint64_t now = config_.clockNs();
		// Marcas de tiempo continuas dentro de un texto; tras una pausa larga se sigue desde ahora.
		// Cada texto nuevo empieza en el momento actual si va retrasado, aunque sea poco: el audio
		// con marcas en el pasado puede hacer que OBS aumente su búfer global para siempre. Nunca
		// se retrocede, así que un texto nuevo tampoco pisa lo ya entregado.
		const uint64_t allowedDelay = firstChunkOfText ? 0 : uint64_t(config_.resyncMs) * kNsPerMs;
		if (now > nextTimestamp + allowedDelay)
			nextTimestamp = now;
		firstChunkOfText = false;
		AudioChunk out;
		out.samples = chunk.data();
		out.frames = static_cast<uint32_t>(frames);
		out.channels = channels;
		out.sampleRate = rate;
		out.timestampNs = nextTimestamp;
		config_.sink(out);
		nextTimestamp += uint64_t(frames) * kNsPerSecond / rate;

		lock.lock();
		const uint64_t after = config_.clockNs();
		if (nextTimestamp > after)
			s.wake.wait_for(lock, std::chrono::nanoseconds(nextTimestamp - after), interrupted);
	}

	if (speaking)
		abandonCurrent();
	++s.generation;
	s.pcm.clear();
	lock.unlock();
	backend.reset();
}

} // namespace tts
