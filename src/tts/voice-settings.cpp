#include "voice-settings.hpp"
#include "speech-text.hpp"
#include "../chat/message.hpp"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

namespace tts {

uint32_t voiceSourceVersion(const QString &typeId)
{
	static const QRegularExpression suffix(QStringLiteral("_v(\\d+)$"));
	const QRegularExpressionMatch match = suffix.match(typeId);
	return match.hasMatch() ? match.captured(1).toUInt() : 0;
}

VoiceMode voiceModeFromString(const QString &mode)
{
	if (mode == QLatin1String("sound"))
		return VoiceMode::Sound;
	if (mode == QLatin1String("both"))
		return VoiceMode::Both;
	return VoiceMode::Read;
}

QString voiceModeToString(VoiceMode mode)
{
	switch (mode) {
	case VoiceMode::Sound:
		return QStringLiteral("sound");
	case VoiceMode::Both:
		return QStringLiteral("both");
	case VoiceMode::Read:
		break;
	}
	return QStringLiteral("read");
}

VoiceSettings VoiceSettings::fromJson(const QJsonObject &json)
{
	VoiceSettings v;
	v.enabled = json.value("enabled").toBool(v.enabled);
	v.mode = voiceModeFromString(json.value("mode").toString());
	v.voice = json.value("voice").toString();
	v.ratePercent = std::clamp(json.value("rate").toInt(v.ratePercent), kMinRate, kMaxRate);
	v.readAuthor = json.value("readAuthor").toBool(v.readAuthor);
	for (const QJsonValue platform : json.value("mutedPlatforms").toArray())
		if (!platform.toString().isEmpty() && !v.mutedPlatforms.contains(platform.toString()))
			v.mutedPlatforms << platform.toString();
	v.maxLength = std::clamp(json.value("maxLength").toInt(v.maxLength), kMinMaxLength, kMaxMaxLength);
	v.soundFile = json.value("soundFile").toString();
	v.soundCooldownMs = std::clamp(json.value("soundCooldown").toInt(v.soundCooldownMs), 0, kMaxCooldownMs);
	return v;
}

QJsonObject VoiceSettings::toJson() const
{
	return QJsonObject{{"enabled", enabled},
			   {"mode", voiceModeToString(mode)},
			   {"voice", voice},
			   {"rate", ratePercent},
			   {"readAuthor", readAuthor},
			   {"mutedPlatforms", QJsonArray::fromStringList(mutedPlatforms)},
			   {"maxLength", maxLength},
			   {"soundFile", soundFile},
			   {"soundCooldown", soundCooldownMs}};
}

bool VoiceSettings::operator==(const VoiceSettings &o) const
{
	return enabled == o.enabled && mode == o.mode && voice == o.voice && ratePercent == o.ratePercent &&
	       readAuthor == o.readAuthor && mutedPlatforms == o.mutedPlatforms && maxLength == o.maxLength &&
	       soundFile == o.soundFile && soundCooldownMs == o.soundCooldownMs;
}

namespace {

// One old source's settings (the obs_data keys of voice-source.cpp) over the defaults of its version.
VoiceSettings fromLegacy(const LegacyVoiceSource &source)
{
	const QJsonObject &s = source.settings;
	VoiceSettings v;
	v.enabled = s.value("enabled").toBool(voiceEnabledByDefault(source.version));
	v.mode = voiceModeFromString(s.value("mode").toString());
	v.voice = s.value("voice").toString();
	v.ratePercent = std::clamp(s.value("rate").toInt(v.ratePercent), VoiceSettings::kMinRate,
				   VoiceSettings::kMaxRate);
	v.readAuthor = s.value("read_author").toBool(v.readAuthor);
	for (auto it = s.begin(); it != s.end(); ++it)
		if (it.key().startsWith(QLatin1String("read_")) && it.key() != QLatin1String("read_author") &&
		    !it.value().toBool(true))
			v.mutedPlatforms << it.key().mid(5);
	v.maxLength = std::clamp(s.value("max_length").toInt(v.maxLength), VoiceSettings::kMinMaxLength,
				 VoiceSettings::kMaxMaxLength);
	v.soundFile = s.value("sound_file").toString();
	v.soundCooldownMs =
		std::clamp(s.value("sound_cooldown").toInt(v.soundCooldownMs), 0, VoiceSettings::kMaxCooldownMs);
	return v;
}

} // namespace

std::optional<VoiceMigration> migrateVoiceSources(const QList<LegacyVoiceSource> &sources)
{
	if (sources.isEmpty())
		return std::nullopt;
	const auto changed = std::find_if(sources.cbegin(), sources.cend(),
					  [](const LegacyVoiceSource &s) { return !s.settings.isEmpty(); });
	VoiceMigration out;
	out.settings = fromLegacy(changed != sources.cend() ? *changed : sources.first());
	for (const LegacyVoiceSource &source : sources)
		out.blockedWords = mergeBlockedWords(
			out.blockedWords, parseBlockedWords(source.settings.value("blocked_words").toString()));
	return out;
}

QStringList mergeBlockedWords(const QStringList &existing, const QStringList &words)
{
	QStringList out = existing;
	QStringList folded;
	for (const QString &word : existing)
		folded << chat::foldText(word.trimmed());
	for (const QString &word : words) {
		const QString trimmed = word.trimmed();
		const QString key = chat::foldText(trimmed);
		if (trimmed.isEmpty() || folded.contains(key))
			continue;
		out << trimmed;
		folded << key;
	}
	return out;
}

} // namespace tts
