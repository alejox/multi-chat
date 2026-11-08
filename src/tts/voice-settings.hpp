#pragma once

// The plugin's voice settings ("voice" in config.json) and the one-time migration from the voice
// sources of older versions, which kept them per source. Qt Core only, so it is tested without
// OBS (tests/test-tts.cpp, tests/test-manager.cpp).

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace tts {

// The voice source type. It is registered twice under the same id: version 0 keeps the scenes
// saved before version 2 loading, and version 2 ("multichat_voice_v2") is what the plugin's own
// voice output uses. Neither appears in "Add Source" any more.
constexpr const char *kVoiceSourceId = "multichat_voice";
constexpr uint32_t kVoiceSourceVersion = 2;

// OBS only saves the values the user changed, so a version 0 source that was never turned on
// has no "enabled" saved: it did not read aloud, while an untouched version 2 one did.
constexpr bool voiceEnabledByDefault(uint32_t version)
{
	return version >= kVoiceSourceVersion;
}

// The version in a voice source's type id: "multichat_voice" is 0, "multichat_voice_v2" is 2.
uint32_t voiceSourceVersion(const QString &typeId);

// What the voice does with each message that passes the filters.
enum class VoiceMode { Read, Sound, Both };
// "read", "sound" or "both"; anything else is Read.
VoiceMode voiceModeFromString(const QString &mode);
QString voiceModeToString(VoiceMode mode);

struct VoiceSettings {
	static constexpr int kMinRate = 0;
	static constexpr int kMaxRate = 100;
	static constexpr int kMinMaxLength = 20;
	static constexpr int kMaxMaxLength = 1000;
	static constexpr int kMaxCooldownMs = 60000;

	bool enabled = false; // off until the user turns it on
	VoiceMode mode = VoiceMode::Read;
	QString voice;          // synthesizer voice id; empty = the system default
	int ratePercent = 50;   // 50 = normal speed
	bool readAuthor = true;
	QStringList mutedPlatforms; // platform ids whose messages are not read
	int maxLength = 200;        // characters of the content, the author not counted
	QString soundFile;          // WAV for the notification sound; empty = the built-in chime
	int soundCooldownMs = 1500; // at most one sound per this interval

	bool readsPlatform(const QString &platform) const { return !mutedPlatforms.contains(platform); }

	// Missing keys keep their default; numbers are clamped to the ranges above.
	static VoiceSettings fromJson(const QJsonObject &json);
	QJsonObject toJson() const;

	bool operator==(const VoiceSettings &other) const;
	bool operator!=(const VoiceSettings &other) const { return !(*this == other); }
};

// A voice source saved in a scene by an older version: its type version and the settings the user
// changed (OBS saves only those; obs_data_get_json gives the same).
struct LegacyVoiceSource {
	uint32_t version = 0;
	QJsonObject settings;
};

struct VoiceMigration {
	VoiceSettings settings;
	QStringList blockedWords; // of every source, in order, without repeats
};

// The settings of the first source the user changed, or of the first source when none was
// changed (its version decides "enabled"). Empty when there are no sources.
std::optional<VoiceMigration> migrateVoiceSources(const QList<LegacyVoiceSource> &sources);

// `words` appended to `existing`, trimmed, skipping blanks and those already there (ignoring case
// and accents, as the filters compare them).
QStringList mergeBlockedWords(const QStringList &existing, const QStringList &words);

} // namespace tts
