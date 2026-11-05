// Fuente "Multichat (voz)". La fuente solo es la unión con OBS: los textos salen de
// speech-text.cpp y el audio de voice-engine.cpp.
//
// Hilos: los mensajes llegan por el hilo principal de Qt, las propiedades por la interfaz y
// OBS puede destruir la fuente desde su propio hilo de destrucción. Por eso no hay conexión de
// Qt por fuente: hay una sola, global, que recorre una lista protegida con candado, y destruir
// una fuente primero la saca de la lista. El motor se destruye siempre fuera del candado porque
// espera a su hilo. Al terminar ese destructor nadie más puede usar la fuente.

#include "voice-source.hpp"
#include "voice-engine.hpp"
#include "speech-text.hpp"
#include "../chat-manager.hpp"
#include "../chat/message.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kDefaultMaxLength = 200;
constexpr int kDefaultRatePercent = 50;

struct VoiceSource {
	obs_source_t *self = nullptr;
	tts::SpeechOptions options;
	std::unique_ptr<tts::VoiceEngine> engine; // solo existe activada
};

std::mutex sourcesMutex;
std::set<VoiceSource *> sources;
bool shuttingDown = false;
QMetaObject::Connection messageConnection;

const char *const kPlatforms[] = {"twitch", "youtube", "kick", "tiktok", "starvios"};

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
	config.onStall = [] { obs_log(LOG_WARNING, "el sintetizador de voz no respondió; se pasa al siguiente mensaje"); };
	return std::make_unique<tts::VoiceEngine>(std::move(config));
}

tts::SpeechOptions readOptions(obs_data_t *settings)
{
	tts::SpeechOptions options;
	options.includeAuthor = obs_data_get_bool(settings, "read_author");
	const QString format = QString::fromUtf8(obs_module_text("Voice.AuthorFormat"));
	if (format.contains(QStringLiteral("%1")) && format.contains(QStringLiteral("%2")))
		options.authorFormat = format;
	for (const char *platform : kPlatforms)
		if (obs_data_get_bool(settings, platformKey(platform).c_str()))
			options.allowedPlatforms.insert(QString::fromUtf8(platform));
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

	std::unique_ptr<tts::VoiceEngine> stopped; // se destruye al salir, ya sin el candado
	std::lock_guard<std::mutex> lock(sourcesMutex);
	s->options = std::move(options);
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
		const QString text = tts::speechTextForMessage(message, s->options);
		if (!text.isEmpty())
			s->engine->enqueue(text);
	}
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

void voice_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "enabled", false);
	obs_data_set_default_string(settings, "voice", "");
	obs_data_set_default_int(settings, "rate", kDefaultRatePercent);
	obs_data_set_default_bool(settings, "read_author", true);
	for (const char *platform : kPlatforms)
		obs_data_set_default_bool(settings, platformKey(platform).c_str(), true);
	obs_data_set_default_int(settings, "max_length", kDefaultMaxLength);
	obs_data_set_default_string(settings, "blocked_words", "");
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

obs_properties_t *voice_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *enabled = obs_properties_add_bool(props, "enabled", obs_module_text("Voice.Enabled"));
	obs_property_set_long_description(enabled, obs_module_text("Voice.Enabled.Help"));

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
	for (const char *platform : kPlatforms) {
		const QString label = readPlatform.arg(chat::platformName(QString::fromUtf8(platform)));
		obs_properties_add_bool(props, platformKey(platform).c_str(), label.toUtf8().constData());
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

	obs_source_info info = {};
	info.id = "multichat_voice";
	info.type = OBS_SOURCE_TYPE_INPUT;
	// Sin duplicar: dos copias independientes leerían cada mensaje dos veces.
	info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
	info.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT;
	info.get_name = voice_get_name;
	info.create = voice_create;
	info.destroy = voice_destroy;
	info.update = voice_update;
	info.deactivate = voice_deactivate;
	info.get_defaults = voice_defaults;
	info.get_properties = voice_properties;
	obs_register_source(&info);
}

void voice_source_shutdown()
{
	QObject::disconnect(messageConnection);
	std::vector<std::unique_ptr<tts::VoiceEngine>> stopped;
	{
		std::lock_guard<std::mutex> lock(sourcesMutex);
		shuttingDown = true;
		for (VoiceSource *s : sources)
			if (s->engine)
				stopped.push_back(std::move(s->engine));
	}
}
