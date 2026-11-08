// Fuente "Multichat (voz)". La fuente solo es la unión con OBS: los textos salen de
// speech-text.cpp y el audio de voice-engine.cpp.
//
// Hilos: los mensajes llegan por el hilo principal de Qt, las propiedades por la interfaz y
// OBS puede destruir la fuente desde su propio hilo de destrucción. Por eso no hay conexión de
// Qt por fuente: hay una sola, global, que recorre una lista protegida con candado, y destruir
// una fuente primero la saca de la lista. El motor se destruye siempre fuera del candado porque
// espera a su hilo. Al terminar ese destructor nadie más puede usar la fuente.

#include "voice-source.hpp"
#include "notification-sound.hpp"
#include "voice-engine.hpp"
#include "speech-text.hpp"
#include "../chat-manager.hpp"
#include "../chat/message.hpp"
#include "../overlay/overlay-source.hpp"
#include "../platforms-registry.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kMaxSoundSeconds = 10.0;
// Setting that marks the plugin's own voice output; any other voice source is an old one.
constexpr const char *kOutputKey = "multichat_output";
// Output channels: 0 is the transition and 1–6 the desktop and microphone audio of OBS's
// settings. The voice takes the highest free one (libobs has MAX_CHANNELS), so plugins that
// count up from 7 do not meet it.
constexpr uint32_t kFirstFreeChannel = 7;

using tts::VoiceMode;

struct VoiceSource {
	obs_source_t *self = nullptr;
	bool output = false; // the plugin's voice output; an old source saved in a scene never speaks
	tts::SpeechOptions options;
	std::unique_ptr<tts::VoiceEngine> engine; // solo existe activada
	VoiceMode mode = VoiceMode::Read;
	std::shared_ptr<const tts::PcmBuffer> sound; // the chime or the user's WAV, at kChimeSampleRate; shared with the queue
	QString soundPath;    // file the sound came from (empty: built-in)
	int cooldownMs = 0;
	uint64_t lastChimeNs = 0;
};

std::mutex sourcesMutex;
std::set<VoiceSource *> sources; // voice outputs only
bool shuttingDown = false;
QMetaObject::Connection messageConnection;
QMetaObject::Connection deletedConnection;
QMetaObject::Connection settingsConnection;

// The plugin's voice output, owned on the Qt main thread (frontend events and settings changes
// arrive there): a strong reference and the output channel it is bound to (0 = none).
obs_source_t *outputSource = nullptr;
uint32_t outputChannel = 0;
bool collectionReady = false; // a scene collection is loaded (between CHANGED and CHANGING)

std::unique_ptr<tts::VoiceEngine> makeEngine(obs_source_t *self)
{
	tts::VoiceEngineConfig config;
	config.backendFactory = [] { return tts::createSpeechBackend(); };
	config.clockNs = [] { return os_gettime_ns(); };
	config.sink = [self](const tts::AudioChunk &chunk) {
		obs_source_audio audio = {};
		audio.data[0] = reinterpret_cast<const uint8_t *>(chunk.samples);
		audio.frames = chunk.frames;
		audio.speakers = chunk.channels == 2 ? SPEAKERS_STEREO : SPEAKERS_MONO;
		audio.format = AUDIO_FORMAT_FLOAT;
		audio.samples_per_sec = chunk.sampleRate;
		audio.timestamp = chunk.timestampNs;
		obs_source_output_audio(self, &audio);
	};
	config.onStall = [] { obs_log(LOG_WARNING, "the speech synthesizer did not respond; moving on to the next message"); };
	return std::make_unique<tts::VoiceEngine>(std::move(config));
}

// The user's WAV, resampled to the delivery rate; the built-in chime when there is none or it
// cannot be read (which the log says).
tts::PcmBuffer loadSound(const QString &path)
{
	if (!path.isEmpty()) {
		QFile file(path);
		tts::PcmBuffer wav;
		if (file.open(QIODevice::ReadOnly))
			wav = tts::parseWav(file.readAll(), kMaxSoundSeconds);
		if (!wav.samples.empty())
			return tts::resamplePcm(wav, tts::kChimeSampleRate);
		obs_log(LOG_WARNING, "notification sound %s could not be read (PCM 16-bit WAV of at most %.0f s expected); using the built-in chime",
			path.toUtf8().constData(), kMaxSoundSeconds);
	}
	return tts::notificationChime(tts::kChimeSampleRate);
}

// The speech filters of the global settings. Blocked words are not here: they live only in the
// Filters, and ChatManager never emits a message they hide.
tts::SpeechOptions speechOptions(const tts::VoiceSettings &settings)
{
	tts::SpeechOptions options;
	options.includeAuthor = settings.readAuthor;
	const QString format = QString::fromUtf8(obs_module_text("Voice.AuthorFormat"));
	if (format.contains(QStringLiteral("%1")) && format.contains(QStringLiteral("%2")))
		options.authorFormat = format;
	for (const platforms::Platform &platform : platforms::kAll)
		if (settings.readsPlatform(QString::fromUtf8(platform.id)))
			options.allowedPlatforms.insert(QString::fromUtf8(platform.id));
	// Los mensajes de prueba solo se reciben si se activan en la ventana de chats, y sirven para
	// escuchar la voz sin estar en directo. Mientras se transmite se descartan en onMessage.
	options.allowedPlatforms.insert(QStringLiteral("demo"));
	options.maxLength = settings.maxLength;
	return options;
}

// Puts the global settings on the plugin's voice output: the engine runs while the voice is on.
void applySettings(VoiceSource *s, const tts::VoiceSettings &settings)
{
	tts::SpeechOptions options = speechOptions(settings);
	const QString soundPath = settings.soundFile;
	// File reading happens outside the lock; only when the sound is used and the path changed.
	std::shared_ptr<const tts::PcmBuffer> sound;
	bool reloadSound = false;
	if (settings.mode != tts::VoiceMode::Read) {
		std::lock_guard<std::mutex> lock(sourcesMutex);
		reloadSound = !s->sound || s->soundPath != soundPath;
	}
	if (reloadSound)
		sound = std::make_shared<const tts::PcmBuffer>(loadSound(soundPath));

	std::unique_ptr<tts::VoiceEngine> stopped; // se destruye al salir, ya sin el candado
	std::lock_guard<std::mutex> lock(sourcesMutex);
	s->options = std::move(options);
	s->mode = settings.mode;
	s->cooldownMs = std::max(0, settings.soundCooldownMs);
	if (reloadSound) {
		s->sound = std::move(sound);
		s->soundPath = soundPath;
	}
	if (settings.enabled && !shuttingDown) {
		if (!s->engine)
			s->engine = makeEngine(s->self);
		s->engine->setVoice(settings.voice, double(settings.ratePercent) / 100.0);
	} else {
		stopped = std::move(s->engine);
	}
}

// What a deletion event needs to recognize a queued message (see chat::deletionMatches).
QJsonObject messageIdentity(const QJsonObject &message)
{
	QJsonObject identity;
	for (const char *key : {"source", "platform", "id", "authorId", "author"})
		identity.insert(QLatin1String(key), message.value(QLatin1String(key)));
	return identity;
}

void onMessage(const QJsonObject &message)
{
	if (message.value(QStringLiteral("platform")).toString() == QStringLiteral("demo") &&
	    obs_frontend_streaming_active())
		return;
	const QJsonObject identity = messageIdentity(message);
	std::lock_guard<std::mutex> lock(sourcesMutex);
	for (VoiceSource *s : sources) {
		// Una fuente que no está en la escena en emisión no se oye: no se gasta nada en ella.
		if (!s->engine || !obs_source_active(s->self))
			continue;
		// The filters (platforms, blocked words, recovered messages) gate every mode: what would
		// not be read makes no sound either.
		const QString text = tts::speechTextForMessage(message, s->options);
		if (text.isEmpty())
			continue;
		std::shared_ptr<const tts::PcmBuffer> sound;
		if (s->mode != VoiceMode::Read) {
			const uint64_t now = os_gettime_ns();
			if (tts::chimeAllowed(now, s->lastChimeNs, s->cooldownMs)) {
				s->lastChimeNs = now;
				sound = s->sound;
			}
		}
		// Sound and text are one queue item: a full queue or a deletion drops both together.
		if (s->mode == VoiceMode::Sound)
			s->engine->enqueueSound(std::move(sound), identity);
		else
			s->engine->enqueue(text, identity, std::move(sound));
	}
}

// A message deleted by moderators (or hidden from the dock), or every message of a banned or
// timed-out user, is not read if it is still waiting: same matching as the history.
void onDeleted(const QJsonObject &deletion)
{
	std::lock_guard<std::mutex> lock(sourcesMutex);
	for (VoiceSource *s : sources)
		if (s->engine)
			s->engine->cancel(deletion);
}

const char *voice_get_name(void *)
{
	return obs_module_text("Voice.Name");
}

void *voice_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new VoiceSource();
	s->self = source;
	s->output = obs_data_get_bool(settings, kOutputKey);
	// The output gets its settings from ChatManager (applySettings) once the plugin adopts it;
	// an old source never gets an engine.
	if (s->output) {
		std::lock_guard<std::mutex> lock(sourcesMutex);
		sources.insert(s);
	}
	return s;
}

void voice_destroy(void *data)
{
	auto *s = static_cast<VoiceSource *>(data);
	std::unique_ptr<tts::VoiceEngine> engine;
	{
		std::lock_guard<std::mutex> lock(sourcesMutex);
		sources.erase(s);
		engine = std::move(s->engine);
	}
	engine.reset(); // espera al hilo: después ya nadie llama a obs_source_output_audio
	delete s;
}

// Al dejar de oírse no tiene sentido seguir leyendo ni dejar textos viejos para luego.
void voice_deactivate(void *data)
{
	auto *s = static_cast<VoiceSource *>(data);
	std::lock_guard<std::mutex> lock(sourcesMutex);
	if (s->engine)
		s->engine->clear();
}

bool voice_open_settings(obs_properties_t *, obs_property_t *, void *)
{
	open_multichat_settings_from_source();
	return false;
}

// The voice is configured in Multichat's settings: the properties only say so. OBS passes the
// source's data here (or nothing for the properties of the type).
obs_properties_t *voice_properties(void *data)
{
	const auto *s = static_cast<const VoiceSource *>(data);
	obs_properties_t *props = obs_properties_create();
	const char *note = s && s->output ? "Voice.OutputNote" : "Voice.LegacyNote";
	obs_property_t *text = obs_properties_add_text(props, "note", obs_module_text(note), OBS_TEXT_INFO);
	obs_property_text_set_info_word_wrap(text, true);
	obs_properties_add_button2(props, "open_settings", obs_module_text("Voice.OpenSettings"), voice_open_settings,
				   nullptr);
	return props;
}

// ---- The plugin's voice output (Qt main thread) ----

VoiceSource *outputData()
{
	return outputSource ? static_cast<VoiceSource *>(obs_obj_get_data(outputSource)) : nullptr;
}

bool isVoiceOutput(obs_source_t *source)
{
	const char *id = obs_source_get_unversioned_id(source);
	if (!id || std::strcmp(id, tts::kVoiceSourceId) != 0)
		return false;
	obs_data_t *settings = obs_source_get_settings(source);
	const bool output = obs_data_get_bool(settings, kOutputKey);
	obs_data_release(settings);
	return output;
}

// Every voice output among the public sources (normally none or the one saved in the scene
// collection), with a reference each.
std::vector<obs_source_t *> voiceOutputs()
{
	std::vector<obs_source_t *> found;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (isVoiceOutput(source))
				if (obs_source_t *ref = obs_source_get_ref(source))
					static_cast<std::vector<obs_source_t *> *>(param)->push_back(ref);
			return true;
		},
		&found);
	return found;
}

// "Multichat (voice)", or with a number when another source already has that name.
std::string uniqueOutputName()
{
	const std::string base = obs_module_text("Voice.Name");
	std::string name = base;
	for (int n = 2; n < 100; ++n) {
		obs_source_t *existing = obs_get_source_by_name(name.c_str());
		if (!existing)
			break;
		obs_source_release(existing);
		name = base + " " + std::to_string(n);
	}
	return name;
}

void saveOutputState()
{
	if (!outputSource)
		return;
	obs_data_t *data = obs_save_source(outputSource);
	const char *json = data ? obs_data_get_json(data) : nullptr;
	if (json)
		ChatManager::instance().setVoiceOutputState(QJsonDocument::fromJson(QByteArray(json)).object());
	obs_data_release(data);
}

// A new voice output with the volume, mute, monitoring, balance, tracks and filters saved last
// time; the first time, heard by the streamer too (Monitor and Output).
obs_source_t *createOutput()
{
	const char *typeId = obs_get_latest_input_type_id(tts::kVoiceSourceId);
	if (!typeId)
		return nullptr;
	const std::string name = uniqueOutputName();
	QJsonObject state = ChatManager::instance().voiceOutputState();
	if (!state.isEmpty()) {
		state.remove(QStringLiteral("uuid")); // a fresh one: the old source may still exist elsewhere
		state.insert(QStringLiteral("name"), QString::fromStdString(name));
		state.insert(QStringLiteral("id"), QString::fromUtf8(tts::kVoiceSourceId));
		state.insert(QStringLiteral("versioned_id"), QString::fromUtf8(typeId));
		QJsonObject settings = state.value(QStringLiteral("settings")).toObject();
		settings.insert(QString::fromUtf8(kOutputKey), true);
		state.insert(QStringLiteral("settings"), settings);
		obs_data_t *data =
			obs_data_create_from_json(QJsonDocument(state).toJson(QJsonDocument::Compact).constData());
		obs_source_t *source = data ? obs_load_source(data) : nullptr;
		obs_data_release(data);
		if (source) {
			obs_source_load2(source);
			return source;
		}
		obs_log(LOG_WARNING, "the saved voice output could not be restored; creating a new one");
	}
	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, kOutputKey, true);
	obs_source_t *source = obs_source_create(typeId, name.c_str(), settings, nullptr);
	obs_data_release(settings);
	if (source)
		obs_source_set_monitoring_enabled(source, true);
	return source;
}

// The voice output on a free output channel, so it sounds in every scene and shows in the mixer.
// A channel taken by something else is never touched.
void bindOutput()
{
	if (outputChannel) {
		obs_source_t *current = obs_get_output_source(outputChannel);
		const bool ours = current == outputSource;
		obs_source_release(current);
		if (ours)
			return;
		outputChannel = 0;
	}
	for (uint32_t channel = MAX_CHANNELS - 1; channel >= kFirstFreeChannel; --channel) {
		obs_source_t *current = obs_get_output_source(channel);
		if (current) {
			obs_log(LOG_INFO, "output channel %u is in use by \"%s\"; trying the next one", channel,
				obs_source_get_name(current));
			obs_source_release(current);
			continue;
		}
		obs_set_output_source(channel, outputSource);
		outputChannel = channel;
		obs_log(LOG_INFO, "voice output \"%s\" on output channel %u", obs_source_get_name(outputSource), channel);
		return;
	}
	obs_log(LOG_WARNING, "no free output channel for the voice: it will not be heard");
}

// Frees the channel and the reference. remove = also take it out of the scene collection (the
// voice was turned off); otherwise OBS is about to clear the collection itself.
void releaseOutput(bool remove)
{
	if (!outputSource)
		return;
	saveOutputState();
	if (outputChannel) {
		obs_source_t *current = obs_get_output_source(outputChannel);
		if (current == outputSource)
			obs_set_output_source(outputChannel, nullptr);
		obs_source_release(current);
		outputChannel = 0;
	}
	std::unique_ptr<tts::VoiceEngine> stopped; // destroyed outside the lock
	if (VoiceSource *s = outputData()) {
		std::lock_guard<std::mutex> lock(sourcesMutex);
		stopped = std::move(s->engine);
	}
	stopped.reset();
	if (remove)
		obs_source_remove(outputSource);
	obs_source_release(outputSource);
	outputSource = nullptr;
}

// Brings the voice output in line with the settings: adopts the one saved in the collection or
// creates it when the voice is on, removes it when off.
void syncOutput()
{
	if (!collectionReady || shuttingDown)
		return;
	const tts::VoiceSettings settings = ChatManager::instance().voiceSettings();
	if (!settings.enabled) {
		releaseOutput(true);
		for (obs_source_t *stray : voiceOutputs()) {
			obs_source_remove(stray);
			obs_source_release(stray);
		}
		return;
	}
	if (!outputSource) {
		std::vector<obs_source_t *> saved = voiceOutputs();
		for (std::size_t i = 0; i < saved.size(); ++i) {
			if (i == 0) {
				outputSource = saved[i];
				continue;
			}
			obs_source_remove(saved[i]); // only one voice: a second one would read everything twice
			obs_source_release(saved[i]);
		}
		if (!outputSource)
			outputSource = createOutput();
		if (!outputSource) {
			obs_log(LOG_WARNING, "the voice output could not be created");
			return;
		}
	}
	bindOutput();
	if (VoiceSource *s = outputData())
		applySettings(s, settings);
}

} // namespace

void register_voice_source()
{
	ChatManager &manager = ChatManager::instance();
	// Solo mensajes nuevos: el historial nunca se lee en voz alta.
	messageConnection = QObject::connect(&manager, &ChatManager::message, &manager, onMessage);
	deletedConnection = QObject::connect(&manager, &ChatManager::deleted, &manager, onDeleted);
	settingsConnection = QObject::connect(&manager, &ChatManager::voiceSettingsChanged, &manager, syncOutput);

	obs_source_info info = {};
	info.id = tts::kVoiceSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	// Never in "Add Source": the plugin creates its own voice output. Without duplicating: two
	// copies would read each message twice.
	info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_CAP_OBSOLETE;
	info.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT;
	info.get_name = voice_get_name;
	info.create = voice_create;
	info.destroy = voice_destroy;
	info.deactivate = voice_deactivate;
	info.get_properties = voice_properties;

	// Version 0: scenes saved before version 2 keep loading it (OBS stores the versioned id).
	obs_source_info legacy = info;
	legacy.output_flags |= OBS_SOURCE_DEPRECATED;
	obs_register_source(&legacy);

	info.version = tts::kVoiceSourceVersion;
	obs_register_source(&info);
}

void voice_migrate_legacy_sources()
{
	QList<tts::LegacyVoiceSource> legacy;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			const char *id = obs_source_get_unversioned_id(source);
			if (!id || std::strcmp(id, tts::kVoiceSourceId) != 0 || isVoiceOutput(source))
				return true;
			obs_data_t *settings = obs_source_get_settings(source);
			// Only the values the user changed: the defaults depend on the version.
			const char *json = obs_data_get_json(settings);
			tts::LegacyVoiceSource entry;
			entry.version = tts::voiceSourceVersion(QString::fromUtf8(obs_source_get_id(source)));
			entry.settings = QJsonDocument::fromJson(QByteArray(json ? json : "{}")).object();
			obs_data_release(settings);
			static_cast<QList<tts::LegacyVoiceSource> *>(param)->append(entry);
			return true;
		},
		&legacy);
	ChatManager::instance().migrateLegacyVoice(legacy);
}

void voice_collection_loaded()
{
	collectionReady = true;
	voice_migrate_legacy_sources(); // may turn the voice on, which syncs through the signal
	syncOutput();
}

void voice_collection_unloading()
{
	collectionReady = false;
	releaseOutput(false);
}

void voice_save_state()
{
	saveOutputState();
}

void voice_skip_current()
{
	std::lock_guard<std::mutex> lock(sourcesMutex);
	for (VoiceSource *s : sources)
		if (s->engine)
			s->engine->skipCurrent();
}

void voice_source_shutdown()
{
	releaseOutput(false); // normally already done when OBS cleared the scene collection
	QObject::disconnect(messageConnection);
	QObject::disconnect(deletedConnection);
	QObject::disconnect(settingsConnection);
	std::vector<std::unique_ptr<tts::VoiceEngine>> stopped;
	{
		std::lock_guard<std::mutex> lock(sourcesMutex);
		shuttingDown = true;
		for (VoiceSource *s : sources)
			if (s->engine)
				stopped.push_back(std::move(s->engine));
	}
}
