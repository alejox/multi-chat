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
#include "../platforms-registry.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QFile>
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

constexpr int kDefaultMaxLength = 200;
constexpr int kDefaultRatePercent = 50;
constexpr int kDefaultCooldownMs = 1500;
constexpr double kMaxSoundSeconds = 10.0;

// What the source does with each message that passes the filters.
enum class Mode { Read, Sound, Both };

Mode modeFromString(const char *mode)
{
	if (mode && std::strcmp(mode, "sound") == 0)
		return Mode::Sound;
	if (mode && std::strcmp(mode, "both") == 0)
		return Mode::Both;
	return Mode::Read;
}

struct VoiceSource {
	obs_source_t *self = nullptr;
	tts::SpeechOptions options;
	std::unique_ptr<tts::VoiceEngine> engine; // solo existe activada
	Mode mode = Mode::Read;
	tts::PcmBuffer sound; // the chime or the user's WAV, at kChimeSampleRate
	QString soundPath;    // file the sound came from (empty: built-in)
	int cooldownMs = kDefaultCooldownMs;
	uint64_t lastChimeNs = 0;
};

std::mutex sourcesMutex;
std::set<VoiceSource *> sources;
bool shuttingDown = false;
QMetaObject::Connection messageConnection;
QMetaObject::Connection deletedConnection;

std::string platformKey(const char *platform)
{
	return std::string("read_") + platform;
}

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

tts::SpeechOptions readOptions(obs_data_t *settings)
{
	tts::SpeechOptions options;
	options.includeAuthor = obs_data_get_bool(settings, "read_author");
	const QString format = QString::fromUtf8(obs_module_text("Voice.AuthorFormat"));
	if (format.contains(QStringLiteral("%1")) && format.contains(QStringLiteral("%2")))
		options.authorFormat = format;
	for (const platforms::Platform &platform : platforms::kAll)
		if (obs_data_get_bool(settings, platformKey(platform.id).c_str()))
			options.allowedPlatforms.insert(QString::fromUtf8(platform.id));
	// Los mensajes de prueba solo se reciben si se activan en la ventana de chats, y sirven para
	// escuchar la voz sin estar en directo. Mientras se transmite se descartan en onMessage.
	options.allowedPlatforms.insert(QStringLiteral("demo"));
	options.maxLength = int(obs_data_get_int(settings, "max_length"));
	options.blockedWords = tts::parseBlockedWords(QString::fromUtf8(obs_data_get_string(settings, "blocked_words")));
	return options;
}

void applySettings(VoiceSource *s, obs_data_t *settings)
{
	tts::SpeechOptions options = readOptions(settings);
	const bool enabled = obs_data_get_bool(settings, "enabled");
	const QString voice = QString::fromUtf8(obs_data_get_string(settings, "voice"));
	const double rate = double(obs_data_get_int(settings, "rate")) / 100.0;
	const Mode mode = modeFromString(obs_data_get_string(settings, "mode"));
	const QString soundPath = QString::fromUtf8(obs_data_get_string(settings, "sound_file"));
	const int cooldownMs = int(obs_data_get_int(settings, "sound_cooldown"));
	// File reading happens outside the lock; only when the sound is used and the path changed.
	tts::PcmBuffer sound;
	bool reloadSound = false;
	if (mode != Mode::Read) {
		std::lock_guard<std::mutex> lock(sourcesMutex);
		reloadSound = s->sound.samples.empty() || s->soundPath != soundPath;
	}
	if (reloadSound)
		sound = loadSound(soundPath);

	std::unique_ptr<tts::VoiceEngine> stopped; // se destruye al salir, ya sin el candado
	std::lock_guard<std::mutex> lock(sourcesMutex);
	s->options = std::move(options);
	s->mode = mode;
	s->cooldownMs = std::max(0, cooldownMs);
	if (reloadSound) {
		s->sound = std::move(sound);
		s->soundPath = soundPath;
	}
	if (enabled && !shuttingDown) {
		if (!s->engine)
			s->engine = makeEngine(s->self);
		s->engine->setVoice(voice, rate);
	} else {
		stopped = std::move(s->engine);
	}
}

void onMessage(const QJsonObject &message)
{
	if (message.value(QStringLiteral("platform")).toString() == QStringLiteral("demo") &&
	    obs_frontend_streaming_active())
		return;
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
		const QString id = message.value(QStringLiteral("id")).toString();
		if (s->mode != Mode::Read) {
			const uint64_t now = os_gettime_ns();
			if (tts::chimeAllowed(now, s->lastChimeNs, s->cooldownMs)) {
				s->lastChimeNs = now;
				s->engine->enqueueSound(s->sound, id);
			}
		}
		if (s->mode != Mode::Sound)
			s->engine->enqueue(text, id);
	}
}

// A message deleted by moderators (or hidden from the dock) is not read if it is still waiting.
void onDeleted(const QJsonObject &deletion)
{
	QSet<QString> ids;
	for (const QJsonValue id : deletion.value(QStringLiteral("ids")).toArray())
		ids.insert(id.toString());
	const QString id = deletion.value(QStringLiteral("id")).toString();
	if (!id.isEmpty())
		ids.insert(id);
	ids.remove(QString());
	if (ids.isEmpty())
		return;
	std::lock_guard<std::mutex> lock(sourcesMutex);
	for (VoiceSource *s : sources)
		if (s->engine)
			s->engine->cancel(ids);
}

const char *voice_get_name(void *)
{
	return obs_module_text("Voice.Name");
}

void *voice_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new VoiceSource();
	s->self = source;
	applySettings(s, settings);
	std::lock_guard<std::mutex> lock(sourcesMutex);
	sources.insert(s);
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

void voice_update(void *data, obs_data_t *settings)
{
	applySettings(static_cast<VoiceSource *>(data), settings);
}

// Al salir de la escena en emisión no tiene sentido seguir leyendo ni dejar textos viejos para luego.
void voice_deactivate(void *data)
{
	auto *s = static_cast<VoiceSource *>(data);
	std::lock_guard<std::mutex> lock(sourcesMutex);
	if (s->engine)
		s->engine->clear();
}

void voice_defaults(obs_data_t *settings, uint32_t version)
{
	obs_data_set_default_bool(settings, "enabled", tts::voiceEnabledByDefault(version));
	obs_data_set_default_string(settings, "mode", "read");
	obs_data_set_default_string(settings, "sound_file", "");
	obs_data_set_default_int(settings, "sound_cooldown", kDefaultCooldownMs);
	obs_data_set_default_string(settings, "voice", "");
	obs_data_set_default_int(settings, "rate", kDefaultRatePercent);
	obs_data_set_default_bool(settings, "read_author", true);
	for (const platforms::Platform &platform : platforms::kAll)
		obs_data_set_default_bool(settings, platformKey(platform.id).c_str(), true);
	obs_data_set_default_int(settings, "max_length", kDefaultMaxLength);
	obs_data_set_default_string(settings, "blocked_words", "");
}

void voice_defaults_v0(obs_data_t *settings)
{
	voice_defaults(settings, 0);
}

void voice_defaults_latest(obs_data_t *settings)
{
	voice_defaults(settings, tts::kVoiceSourceVersion);
}

// OBS pasa aquí los datos de la fuente (que sigue viva mientras la ventana esté abierta), o
// nada si son las propiedades sin fuente.
bool voice_skip_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	auto *s = static_cast<VoiceSource *>(data);
	if (!s)
		return false;
	std::lock_guard<std::mutex> lock(sourcesMutex);
	if (s->engine)
		s->engine->skipCurrent();
	return false;
}

// The sound options only show when a sound is used; the voice ones, when the chat is read.
bool voice_mode_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const Mode mode = modeFromString(obs_data_get_string(settings, "mode"));
	for (const char *key : {"sound_file", "sound_cooldown"})
		obs_property_set_visible(obs_properties_get(props, key), mode != Mode::Read);
	for (const char *key : {"voice", "rate", "read_author", "max_length", "skip"})
		obs_property_set_visible(obs_properties_get(props, key), mode != Mode::Sound);
	return true;
}

obs_properties_t *voice_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *enabled = obs_properties_add_bool(props, "enabled", obs_module_text("Voice.Enabled"));
	obs_property_set_long_description(enabled, obs_module_text("Voice.Enabled.Help"));

	obs_property_t *mode = obs_properties_add_list(props, "mode", obs_module_text("Voice.Mode"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
	for (const char *id : {"read", "sound", "both"})
		obs_property_list_add_string(mode, obs_module_text(("Voice.Mode." + std::string(id)).c_str()), id);
	obs_property_set_long_description(mode, obs_module_text("Voice.Mode.Help"));
	obs_property_set_modified_callback(mode, voice_mode_changed);
	obs_property_t *soundFile = obs_properties_add_path(props, "sound_file", obs_module_text("Voice.SoundFile"),
							    OBS_PATH_FILE, obs_module_text("Voice.SoundFilter"), nullptr);
	obs_property_set_long_description(soundFile, obs_module_text("Voice.SoundFile.Help"));
	obs_property_t *cooldown =
		obs_properties_add_int(props, "sound_cooldown", obs_module_text("Voice.SoundCooldown"), 0, 60000, 100);
	obs_property_int_set_suffix(cooldown, " ms");
	obs_property_set_long_description(cooldown, obs_module_text("Voice.SoundCooldown.Help"));

	obs_property_t *voice = obs_properties_add_list(props, "voice", obs_module_text("Voice.Voice"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(voice, obs_module_text("Voice.DefaultVoice"), "");
	for (const tts::VoiceInfo &info : tts::availableVoices())
		obs_property_list_add_string(voice, info.name.toUtf8().constData(), info.id.toUtf8().constData());

	obs_property_t *rate = obs_properties_add_int_slider(props, "rate", obs_module_text("Voice.Rate"), 0, 100, 1);
	obs_property_int_set_suffix(rate, " %");
	obs_property_set_long_description(rate, obs_module_text("Voice.Rate.Help"));

	obs_properties_add_bool(props, "read_author", obs_module_text("Voice.ReadAuthor"));
	const QString readPlatform = QString::fromUtf8(obs_module_text("Voice.ReadPlatform"));
	for (const platforms::Platform &platform : platforms::kAll) {
		const QString label = readPlatform.arg(chat::platformName(QString::fromUtf8(platform.id)));
		obs_properties_add_bool(props, platformKey(platform.id).c_str(), label.toUtf8().constData());
	}

	obs_property_t *maxLength =
		obs_properties_add_int(props, "max_length", obs_module_text("Voice.MaxLength"), 20, 1000, 10);
	obs_property_set_long_description(maxLength, obs_module_text("Voice.MaxLength.Help"));
	obs_property_t *blocked = obs_properties_add_text(props, "blocked_words", obs_module_text("Voice.BlockedWords"),
							  OBS_TEXT_MULTILINE);
	obs_property_set_long_description(blocked, obs_module_text("Voice.BlockedWords.Help"));

	obs_properties_add_button2(props, "skip", obs_module_text("Voice.Skip"), voice_skip_clicked, data);
	return props;
}

} // namespace

void register_voice_source()
{
	ChatManager &manager = ChatManager::instance();
	// Solo mensajes nuevos: el historial nunca se lee en voz alta.
	messageConnection = QObject::connect(&manager, &ChatManager::message, &manager, onMessage);
	deletedConnection = QObject::connect(&manager, &ChatManager::deleted, &manager, onDeleted);

	obs_source_info info = {};
	info.id = tts::kVoiceSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	// Sin duplicar: dos copias independientes leerían cada mensaje dos veces.
	info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
	info.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT;
	info.get_name = voice_get_name;
	info.create = voice_create;
	info.destroy = voice_destroy;
	info.update = voice_update;
	info.deactivate = voice_deactivate;
	info.get_properties = voice_properties;

	// Version 0: the scenes saved before keep loading it (OBS stores the versioned id) with
	// "enabled" off by default; it no longer appears in "Add Source".
	obs_source_info legacy = info;
	legacy.output_flags |= OBS_SOURCE_DEPRECATED | OBS_SOURCE_CAP_OBSOLETE;
	legacy.get_defaults = voice_defaults_v0;
	obs_register_source(&legacy);

	info.version = tts::kVoiceSourceVersion;
	info.get_defaults = voice_defaults_latest;
	obs_register_source(&info);
}

void voice_source_shutdown()
{
	QObject::disconnect(messageConnection);
	QObject::disconnect(deletedConnection);
	std::vector<std::unique_ptr<tts::VoiceEngine>> stopped;
	{
		std::lock_guard<std::mutex> lock(sourcesMutex);
		shuttingDown = true;
		for (VoiceSource *s : sources)
			if (s->engine)
				stopped.push_back(std::move(s->engine));
	}
}
