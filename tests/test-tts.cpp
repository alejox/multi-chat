// Pruebas de la lectura del chat en voz alta (sin OBS ni sintetizador de voz).

#include "../src/chat/message.hpp"
#include "../src/tts/bounded-queue.hpp"
#include "../src/tts/pcm.hpp"
#include "../src/tts/speech-text.hpp"
#include "../src/tts/voice-engine.hpp"

#include <QElapsedTimer>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

using namespace tts;

namespace {

SpeechOptions baseOptions()
{
	SpeechOptions o;
	o.includeAuthor = false;
	o.authorFormat = QStringLiteral("%1 dice: %2");
	o.allowedPlatforms = {"twitch", "youtube", "kick", "tiktok"};
	o.maxLength = 0;
	return o;
}

QJsonObject message(const QString &text, const QString &author = QStringLiteral("Ana"),
		    const QString &platform = QStringLiteral("twitch"), const chat::MessageOptions &options = {})
{
	QJsonArray parts;
	if (!text.isEmpty())
		parts.append(chat::textPart(text));
	return chat::makeMessage(platform, "canal", author, parts, options);
}

// Sintetizador falso. Su estado sobrevive al motor para poder llamar a las funciones tarde,
// como puede hacer el sintetizador real.
struct FakeSpeech {
	struct Job {
		SpeechRequest request;
		SpeechBackend::PcmCallback onPcm;
		SpeechBackend::DoneCallback onDone;
	};
	std::mutex mutex;
	std::vector<Job> jobs;
	std::atomic<int> created{0};
	std::atomic<int> stops{0};
	// Milisegundos de audio que se entregan de golpe al empezar cada texto; -1 = nada.
	std::atomic<int> autoRenderMs{-1};

	int jobCount()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return int(jobs.size());
	}
	Job job(int index)
	{
		std::lock_guard<std::mutex> lock(mutex);
		return jobs.at(std::size_t(index));
	}
};

PcmBuffer silence(int ms, uint32_t channels = 1, uint32_t rate = 1000)
{
	PcmBuffer b;
	b.sampleRate = rate;
	b.channels = channels;
	b.samples.assign(std::size_t(ms) * rate / 1000 * channels, 0.25f);
	return b;
}

class FakeBackend : public SpeechBackend {
public:
	explicit FakeBackend(std::shared_ptr<FakeSpeech> state) : state_(std::move(state)) { ++state_->created; }
	void speak(const SpeechRequest &request, PcmCallback onPcm, DoneCallback onDone) override
	{
		{
			std::lock_guard<std::mutex> lock(state_->mutex);
			state_->jobs.push_back({request, onPcm, onDone});
		}
		if (state_->autoRenderMs >= 0) {
			onPcm(silence(state_->autoRenderMs));
			onDone();
		}
	}
	void stop() override { ++state_->stops; }

private:
	std::shared_ptr<FakeSpeech> state_;
};

uint64_t steadyNs()
{
	return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
				.count());
}

// Motor conectado al sintetizador falso, que guarda lo que sale.
struct Rig {
	struct Chunk {
		uint32_t frames;
		uint32_t rate;
		uint64_t timestampNs;
		qint64 wallMs;
	};
	std::shared_ptr<FakeSpeech> speech = std::make_shared<FakeSpeech>();
	std::mutex mutex;
	std::vector<Chunk> chunks;
	std::atomic<int> stalls{0};
	QElapsedTimer wall;
	std::unique_ptr<VoiceEngine> engine;

	explicit Rig(VoiceEngineConfig config = {})
	{
		wall.start();
		auto state = speech;
		config.backendFactory = [state] { return std::make_unique<FakeBackend>(state); };
		config.sink = [this](const AudioChunk &c) {
			std::lock_guard<std::mutex> lock(mutex);
			chunks.push_back({c.frames, c.sampleRate, c.timestampNs, wall.elapsed()});
		};
		config.clockNs = steadyNs;
		config.onStall = [this] { ++stalls; };
		engine = std::make_unique<VoiceEngine>(std::move(config));
	}
	uint64_t frames()
	{
		std::lock_guard<std::mutex> lock(mutex);
		uint64_t total = 0;
		for (const Chunk &c : chunks)
			total += c.frames;
		return total;
	}
	std::vector<Chunk> snapshot()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return chunks;
	}
};

template<typename Pred> bool waitFor(Pred pred, int timeoutMs = 5000)
{
	QElapsedTimer timer;
	timer.start();
	while (!pred()) {
		if (timer.elapsed() > timeoutMs)
			return false;
		QThread::msleep(2);
	}
	return true;
}

VoiceEngineConfig fastConfig()
{
	VoiceEngineConfig config;
	config.gapMs = 0;
	return config;
}

} // namespace

class TtsTest : public QObject {
	Q_OBJECT

private slots:
	void plainTextIsSpoken() { QCOMPARE(speechTextForMessage(message("hola a todos"), baseOptions()), QString("hola a todos")); }

	void authorPrefixIsOptional()
	{
		SpeechOptions o = baseOptions();
		QCOMPARE(speechTextForMessage(message("hola"), o), QString("hola"));
		o.includeAuthor = true;
		QCOMPARE(speechTextForMessage(message("hola"), o), QString("Ana dice: hola"));
	}

	// Un autor o un texto con "%2" o "%1" no se puede colar en el formato.
	void formatMarkersInUserTextAreLiteral()
	{
		SpeechOptions o = baseOptions();
		o.includeAuthor = true;
		QCOMPARE(speechTextForMessage(message("el 50%1 gana", "Eva%2"), o), QString("Eva%2 dice: el 50%1 gana"));
	}

	void emoteAndTextPartsAreJoined()
	{
		QJsonArray parts;
		parts.append(chat::textPart("hola"));
		parts.append(chat::emotePart("https://x/y.png", "Kappa"));
		parts.append(chat::textPart("mundo"));
		QCOMPARE(speechTextForMessage(chat::makeMessage("twitch", "c", "Ana", parts), baseOptions()), QString("hola mundo"));
	}

	void onlyEmotesIsNotSpoken()
	{
		QJsonArray parts;
		parts.append(chat::emotePart("https://x/y.png", "Kappa"));
		QVERIFY(speechTextForMessage(chat::makeMessage("twitch", "c", "Ana", parts), baseOptions()).isEmpty());
	}

	void linksAreRemoved()
	{
		QCOMPARE(cleanSpeechText("mira https://a.com/x?y=1 y HTTP://b.org ya www.c.net/z fin"), QString("mira y ya fin"));
		QCOMPARE(cleanSpeechText("awww.que lindo"), QString("awww.que lindo"));
		QVERIFY(speechTextForMessage(message("https://a.com"), baseOptions()).isEmpty());
	}

	void emojisAreRemovedButAccentsStay()
	{
		QCOMPARE(cleanSpeechText(QString::fromUtf8("hola😀mundo")), QString("hola mundo"));
		QCOMPARE(cleanSpeechText(QString::fromUtf8("🔥 ¡qué señal, niño! ❤️ ⭐")), QString::fromUtf8("¡qué señal, niño!"));
		QCOMPARE(cleanSpeechText(QString::fromUtf8("👨‍👩‍👧 familia 🇨🇴")), QString("familia"));
		QCOMPARE(cleanSpeechText(QString::fromUtf8("日本語 Привет")), QString::fromUtf8("日本語 Привет"));
		QVERIFY(speechTextForMessage(message(QString::fromUtf8("😀😀 🔥")), baseOptions()).isEmpty());
	}

	void whitespaceIsCollapsed() { QCOMPARE(cleanSpeechText("  a \n\t b   c  "), QString("a b c")); }

	void withoutLettersOrDigitsIsNotSpoken()
	{
		QVERIFY(speechTextForMessage(message("?!... ---"), baseOptions()).isEmpty());
		QVERIFY(speechTextForMessage(message("   "), baseOptions()).isEmpty());
		QCOMPARE(speechTextForMessage(message("7"), baseOptions()), QString("7"));
		// El autor por sí solo tampoco es motivo para hablar.
		SpeechOptions o = baseOptions();
		o.includeAuthor = true;
		QVERIFY(speechTextForMessage(message("!!!"), o).isEmpty());
	}

	void longTextIsCutAtAWordBoundary()
	{
		SpeechOptions o = baseOptions();
		o.maxLength = 10;
		QCOMPARE(speechTextForMessage(message("uno dos tres cuatro"), o), QString("uno dos"));
		QCOMPARE(speechTextForMessage(message("uno dos tr"), o), QString("uno dos tr"));
		// El espacio justo en el límite también es un corte limpio.
		QCOMPARE(speechTextForMessage(message("uno dos tr es"), o), QString("uno dos tr"));
		// Una sola palabra enorme se corta donde toque.
		QCOMPARE(speechTextForMessage(message("abcdefghijklmnop"), o), QString("abcdefghij"));
	}

	void cutNeverSplitsASurrogatePair()
	{
		SpeechOptions o = baseOptions();
		o.maxLength = 4;
		const QString cut = speechTextForMessage(message(QString::fromUtf8("abc𝒳𝒳𝒳")), o);
		QCOMPARE(cut, QString("abc"));
	}

	void authorDoesNotCountTowardsMaxLength()
	{
		SpeechOptions o = baseOptions();
		o.includeAuthor = true;
		o.maxLength = 4;
		QCOMPARE(speechTextForMessage(message("hola que tal"), o), QString("Ana dice: hola"));
	}

	void blockedWordsRejectTheWholeMessage()
	{
		SpeechOptions o = baseOptions();
		o.blockedWords = {"Tonto", "muy feo"};
		QVERIFY(speechTextForMessage(message("eres TONTO, de verdad"), o).isEmpty());
		QVERIFY(speechTextForMessage(message("es muy  feo eso"), o).isEmpty());
		// Palabra completa: que aparezca dentro de otra no cuenta.
		QCOMPARE(speechTextForMessage(message("tontos atontado"), o), QString("tontos atontado"));
		// Mayúsculas acentuadas y letras que no son ASCII como frontera de palabra.
		o.blockedWords = {"canción"};
		QVERIFY(speechTextForMessage(message("otra CANCIÓN"), o).isEmpty());
		o.blockedWords = {"can"};
		QCOMPARE(speechTextForMessage(message("cañcan"), o), QString::fromUtf8("cañcan"));
	}

	void blockedWordsApplyBeyondTheCutAndToAuthor()
	{
		SpeechOptions o = baseOptions();
		o.blockedWords = {"secreto"};
		o.maxLength = 5;
		QVERIFY(speechTextForMessage(message("hola que tal secreto"), o).isEmpty());
		o.maxLength = 0;
		// El nombre solo importa si se va a leer.
		QCOMPARE(speechTextForMessage(message("hola", "secreto"), o), QString("hola"));
		o.includeAuthor = true;
		QVERIFY(speechTextForMessage(message("hola", "secreto"), o).isEmpty());
	}

	// Bloquear una palabra del formato ("dice") no puede silenciar todos los mensajes.
	void blockedWordsIgnoreTheAuthorFormat()
	{
		SpeechOptions o = baseOptions();
		o.includeAuthor = true;
		o.blockedWords = {"dice"};
		QCOMPARE(speechTextForMessage(message("hola"), o), QString("Ana dice: hola"));
		// Sigue contando si lo escribe la persona, en el mensaje o en su nombre.
		QVERIFY(speechTextForMessage(message("ella dice hola"), o).isEmpty());
		QVERIFY(speechTextForMessage(message("hola", "dice"), o).isEmpty());
		// Una frase no se forma con el final del nombre y el texto del formato.
		o.blockedWords = {"Ana dice"};
		QCOMPARE(speechTextForMessage(message("hola"), o), QString("Ana dice: hola"));
	}

	void blockedWordsWithRegexCharactersAreLiteral()
	{
		SpeechOptions o = baseOptions();
		o.blockedWords = {"a.b", "c++"};
		QCOMPARE(speechTextForMessage(message("axb"), o), QString("axb"));
		QVERIFY(speechTextForMessage(message("uso c++ siempre"), o).isEmpty());
	}

	void parseBlockedWordsSplitsAndTrims()
	{
		QCOMPARE(parseBlockedWords(" uno, dos tres\n\ncuatro ,, "), QStringList({"uno", "dos tres", "cuatro"}));
		QVERIFY(parseBlockedWords("  ").isEmpty());
	}

	void platformFilter()
	{
		SpeechOptions o = baseOptions();
		o.allowedPlatforms = {"kick"};
		QVERIFY(speechTextForMessage(message("hola", "Ana", "twitch"), o).isEmpty());
		QCOMPARE(speechTextForMessage(message("hola", "Ana", "kick"), o), QString("hola"));
		o.allowedPlatforms.clear();
		QVERIFY(speechTextForMessage(message("hola", "Ana", "kick"), o).isEmpty());
	}

	void actionReadsAuthorAsSubject()
	{
		chat::MessageOptions action;
		action.action = true;
		SpeechOptions o = baseOptions();
		QCOMPARE(speechTextForMessage(message("saluda", "Ana", "twitch", action), o), QString("saluda"));
		o.includeAuthor = true;
		QCOMPARE(speechTextForMessage(message("saluda", "Ana", "twitch", action), o), QString("Ana saluda"));
	}

	void systemTextIsSpokenWithoutDoublingTheAuthor()
	{
		chat::MessageOptions sys;
		sys.kind = "highlight";
		sys.system = "Ana se suscribió por 6 meses";
		SpeechOptions o = baseOptions();
		o.includeAuthor = true;
		QCOMPARE(speechTextForMessage(message("", "Ana", "twitch", sys), o), QString("Ana se suscribió por 6 meses"));
		QCOMPARE(speechTextForMessage(message("gracias", "Ana", "twitch", sys), o),
			 QString("Ana se suscribió por 6 meses. gracias"));

		sys.system = "Miembro hace 6 meses!";
		QCOMPARE(speechTextForMessage(message("gracias", "Ana", "youtube", sys), o),
			 QString("Ana, Miembro hace 6 meses! gracias"));
		o.includeAuthor = false;
		QCOMPARE(speechTextForMessage(message("", "Ana", "youtube", sys), o), QString("Miembro hace 6 meses!"));
	}

	void systemTextIsFilteredToo()
	{
		chat::MessageOptions sys;
		sys.system = "Ana envió secreto x3";
		SpeechOptions o = baseOptions();
		o.blockedWords = {"secreto"};
		QVERIFY(speechTextForMessage(message("", "Ana", "tiktok", sys), o).isEmpty());
	}

	void authorWithOnlyEmojisHasNoPrefix()
	{
		SpeechOptions o = baseOptions();
		o.includeAuthor = true;
		QCOMPARE(speechTextForMessage(message("hola", QString::fromUtf8("😀😀")), o), QString("hola"));
		QCOMPARE(speechTextForMessage(message("hola", QString::fromUtf8("Ana 😀")), o), QString("Ana dice: hola"));
	}

	void queueIsFifo()
	{
		BoundedQueue<int> q(3);
		QVERIFY(q.empty());
		QCOMPARE(q.push(1), std::size_t(0));
		QCOMPARE(q.push(2), std::size_t(0));
		int v = 0;
		QVERIFY(q.pop(v));
		QCOMPARE(v, 1);
		QVERIFY(q.pop(v));
		QCOMPARE(v, 2);
		QVERIFY(!q.pop(v));
	}

	void queueDropsTheOldestWhenFull()
	{
		BoundedQueue<int> q(2);
		q.push(1);
		q.push(2);
		QCOMPARE(q.push(3), std::size_t(1));
		QCOMPARE(q.push(4), std::size_t(1));
		QCOMPARE(q.droppedTotal(), std::size_t(2));
		QCOMPARE(q.size(), std::size_t(2));
		int v = 0;
		QVERIFY(q.pop(v));
		QCOMPARE(v, 3);
	}

	void queueCapacityChangesAndClear()
	{
		BoundedQueue<int> q(4);
		for (int i = 0; i < 4; ++i)
			q.push(i);
		QCOMPARE(q.setCapacity(1), std::size_t(3));
		int v = 0;
		QVERIFY(q.pop(v));
		QCOMPARE(v, 3);
		q.setCapacity(0); // un límite inválido se queda en uno
		QCOMPARE(q.capacity(), std::size_t(1));
		q.push(1);
		q.clear();
		QVERIFY(q.empty());
	}

	void pcmKeepsFloatAndScalesIntegers()
	{
		const float f[] = {0.5f, -0.5f};
		const float *fp[] = {f};
		PcmBuffer a = toFloatPcm(fp, 1, 2, 1, false, 22050);
		QCOMPARE(a.sampleRate, uint32_t(22050));
		QCOMPARE(a.frames(), std::size_t(2));
		QCOMPARE(a.samples[1], -0.5f);

		const int16_t i[] = {16384, -32768};
		const int16_t *ip[] = {i};
		PcmBuffer b = toFloatPcm(ip, 1, 2, 1, false, 48000);
		QCOMPARE(b.samples[0], 0.5f);
		QCOMPARE(b.samples[1], -1.0f);

		const int32_t l[] = {1073741824};
		const int32_t *lp[] = {l};
		QCOMPARE(toFloatPcm(lp, 1, 1, 1, false, 48000).samples[0], 0.5f);

		const double d[] = {0.25};
		const double *dp[] = {d};
		QCOMPARE(toFloatPcm(dp, 1, 1, 1, false, 48000).samples[0], 0.25f);
	}

	void pcmInterleavesPlanarStereo()
	{
		const float l[] = {1.0f, 2.0f};
		const float r[] = {3.0f, 4.0f};
		const float *planar[] = {l, r};
		PcmBuffer a = toFloatPcm(planar, 2, 2, 2, false, 22050);
		QCOMPARE(a.channels, uint32_t(2));
		QCOMPARE(a.samples, (std::vector<float>{1.0f, 3.0f, 2.0f, 4.0f}));

		const float mixed[] = {1.0f, 3.0f, 2.0f, 4.0f};
		const float *inter[] = {mixed};
		QCOMPARE(toFloatPcm(inter, 1, 2, 2, true, 22050).samples, a.samples);
	}

	void pcmDownmixesMoreThanTwoChannels()
	{
		const int16_t all[] = {16384, 16384, 16384, 0, 0, 0};
		const int16_t *inter[] = {all};
		PcmBuffer a = toFloatPcm(inter, 1, 2, 3, true, 44100);
		QCOMPARE(a.channels, uint32_t(1));
		QCOMPARE(a.samples, (std::vector<float>{0.5f, 0.0f}));
	}

	void pcmRejectsInvalidBuffers()
	{
		const float f[] = {0.5f};
		const float *fp[] = {f};
		const float *none[] = {nullptr};
		QVERIFY(toFloatPcm(fp, 1, 0, 1, false, 22050).samples.empty());
		QVERIFY(toFloatPcm(fp, 1, 1, 0, false, 22050).samples.empty());
		QVERIFY(toFloatPcm(fp, 1, 1, 1, false, 0).samples.empty());
		QVERIFY(toFloatPcm(fp, 1, 1, 2, false, 22050).samples.empty()); // falta un plano
		QVERIFY(toFloatPcm(none, 1, 1, 1, false, 22050).samples.empty());
	}

	// Sin textos no hay sintetizador: la fuente activada pero sin chat no gasta nada.
	void backendIsCreatedOnFirstText()
	{
		Rig rig(fastConfig());
		QThread::msleep(20);
		QCOMPARE(rig.speech->created.load(), 0);
		rig.speech->autoRenderMs = 20;
		rig.engine->enqueue("hola");
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 1; }));
		QCOMPARE(rig.speech->created.load(), 1);
	}

	// El audio llega de golpe y debe salir a ritmo real, en trozos pequeños y en orden.
	void audioIsDeliveredAtRealTimePace()
	{
		Rig rig(fastConfig());
		rig.speech->autoRenderMs = 300;
		rig.engine->enqueue("hola");
		QVERIFY(waitFor([&] { return rig.frames() == 300; }));

		const auto chunks = rig.snapshot();
		QVERIFY(chunks.size() >= 10);
		for (std::size_t i = 0; i < chunks.size(); ++i) {
			QVERIFY(chunks[i].frames <= 20); // 20 ms a 1000 Hz
			QCOMPARE(chunks[i].rate, uint32_t(1000));
			if (i > 0) {
				QVERIFY(chunks[i].timestampNs > chunks[i - 1].timestampNs);
				const uint64_t expected = uint64_t(chunks[i - 1].frames) * 1000000u;
				QVERIFY(chunks[i].timestampNs - chunks[i - 1].timestampNs >= expected);
			}
		}
		// Del primer al último trozo pasan al menos los 280 ms de audio que hay entre ellos,
		// con un margen por el redondeo del temporizador.
		QVERIFY(chunks.back().wallMs - chunks.front().wallMs >= 260);
	}

	void textsAreSpokenOneAfterAnother()
	{
		Rig rig(fastConfig());
		rig.engine->enqueue("uno");
		rig.engine->enqueue("dos");
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 1; }));
		QCOMPARE(rig.speech->job(0).request.text, QString("uno"));

		// Sin terminar de generar, el segundo espera; aunque el audio se haya entregado.
		rig.speech->job(0).onPcm(silence(40));
		QVERIFY(waitFor([&] { return rig.frames() == 40; }));
		QThread::msleep(30);
		QCOMPARE(rig.speech->jobCount(), 1);

		rig.speech->job(0).onDone();
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 2; }));
		QCOMPARE(rig.speech->job(1).request.text, QString("dos"));
	}

	// Un búfer con otro formato a mitad de un texto se entrega con su propio formato.
	void formatChangesAreRespected()
	{
		Rig rig(fastConfig());
		rig.engine->enqueue("uno");
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 1; }));
		rig.speech->job(0).onPcm(silence(20, 1, 1000));
		rig.speech->job(0).onPcm(silence(20, 2, 2000));
		QVERIFY(waitFor([&] { return rig.frames() == 60; }));
		const auto chunks = rig.snapshot();
		QCOMPARE(chunks.front().rate, uint32_t(1000));
		QCOMPARE(chunks.back().rate, uint32_t(2000));
	}

	void voiceAndRateApplyToTheNextText()
	{
		Rig rig(fastConfig());
		rig.engine->setVoice("voz-a", 0.75);
		rig.engine->enqueue("uno");
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 1; }));
		QCOMPARE(rig.speech->job(0).request.voiceId, QString("voz-a"));
		QCOMPARE(rig.speech->job(0).request.rate, 0.75);
	}

	void skipDrainsTheCurrentAudioAndContinues()
	{
		Rig rig(fastConfig());
		rig.engine->enqueue("largo");
		rig.engine->enqueue("siguiente");
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 1; }));
		const auto first = rig.speech->job(0);
		first.onPcm(silence(5000));
		QVERIFY(waitFor([&] { return rig.frames() >= 40; }));

		rig.engine->skipCurrent();
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 2; }));
		QCOMPARE(rig.speech->stops.load(), 1);
		QCOMPARE(rig.speech->job(1).request.text, QString("siguiente"));
		const uint64_t after = rig.frames();
		QVERIFY(after < 1000);

		// Lo que llegue tarde del texto cortado se ignora y no da por terminado al nuevo.
		first.onPcm(silence(1000));
		first.onDone();
		QThread::msleep(40);
		QCOMPARE(rig.frames(), after);
		rig.speech->job(1).onPcm(silence(40));
		QVERIFY(waitFor([&] { return rig.frames() == after + 40; }));
	}

	void skipWithNothingPlayingIsHarmless()
	{
		Rig rig(fastConfig());
		rig.engine->skipCurrent();
		rig.engine->clear();
		QThread::msleep(10);
		QCOMPARE(rig.speech->created.load(), 0);
		rig.engine->enqueue("hola");
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 1; }));
	}

	void clearEmptiesTheQueueAndStopsSpeech()
	{
		Rig rig(fastConfig());
		rig.engine->enqueue("uno");
		rig.engine->enqueue("dos");
		rig.engine->enqueue("tres");
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 1; }));
		rig.speech->job(0).onPcm(silence(5000));
		QVERIFY(waitFor([&] { return rig.frames() >= 20; }));
		rig.engine->clear();
		QVERIFY(waitFor([&] { return rig.speech->stops.load() == 1; }));
		QThread::msleep(60);
		QCOMPARE(rig.speech->jobCount(), 1);
		const uint64_t frames = rig.frames();
		QThread::msleep(40);
		QCOMPARE(rig.frames(), frames);
	}

	void fullQueueDropsTheOldestText()
	{
		VoiceEngineConfig config = fastConfig();
		config.queueLimit = 2;
		Rig rig(config);
		rig.engine->enqueue("actual");
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 1; }));
		QCOMPARE(rig.engine->enqueue("a"), std::size_t(0));
		QCOMPARE(rig.engine->enqueue("b"), std::size_t(0));
		QCOMPARE(rig.engine->enqueue("c"), std::size_t(1));
		QCOMPARE(rig.engine->droppedTotal(), std::size_t(1));
		rig.speech->job(0).onDone();
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 2; }));
		QCOMPARE(rig.speech->job(1).request.text, QString("b"));
	}

	// Si el sintetizador se queda callado no debe bloquear la lectura para siempre.
	void stalledBackendIsAbandoned()
	{
		VoiceEngineConfig config = fastConfig();
		config.stallMs = 50;
		Rig rig(config);
		rig.engine->enqueue("mudo");
		rig.engine->enqueue("siguiente");
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 2; }));
		QCOMPARE(rig.speech->job(1).request.text, QString("siguiente"));
		QVERIFY(rig.stalls.load() >= 1);
		QVERIFY(rig.speech->stops.load() >= 1);
	}

	// Destruir la fuente en plena lectura: se detiene el sintetizador, no sale más audio y las
	// funciones que el sintetizador aún tenga guardadas se pueden llamar sin peligro.
	void destroyingWhilePlayingIsSafe()
	{
		Rig rig(fastConfig());
		rig.engine->enqueue("largo");
		rig.engine->enqueue("otro");
		QVERIFY(waitFor([&] { return rig.speech->jobCount() == 1; }));
		const auto job = rig.speech->job(0);
		job.onPcm(silence(5000));
		QVERIFY(waitFor([&] { return rig.frames() >= 20; }));

		QElapsedTimer timer;
		timer.start();
		rig.engine.reset();
		QVERIFY(timer.elapsed() < 1000); // no espera a que termine de hablar
		QCOMPARE(rig.speech->stops.load(), 1);

		const uint64_t frames = rig.frames();
		job.onPcm(silence(100));
		job.onDone();
		QThread::msleep(40);
		QCOMPARE(rig.frames(), frames);
		QCOMPARE(rig.speech->jobCount(), 1);
	}

	void destroyingAnIdleEngineCreatesNoBackend()
	{
		Rig rig(fastConfig());
		rig.engine.reset();
		QCOMPARE(rig.speech->created.load(), 0);
		QCOMPARE(rig.speech->stops.load(), 0);
	}
};

QTEST_GUILESS_MAIN(TtsTest)
#include "test-tts.moc"
