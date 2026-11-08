#pragma once

// Pure helpers for the overlay sources: CSS values built from OBS settings. No OBS or Qt
// types here, so the tests can use them without linking libobs.

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace overlay {

// Font stack the pages use when no family is chosen (and as fallback after a chosen one).
extern const char *const kDefaultFontStack;

// "rgba(r, g, b, a)" from an OBS color (0xAABBGGRR). Without alpha the color is always
// opaque: OBS color properties without an alpha channel do not guarantee the top byte.
std::string cssColor(uint32_t abgr, bool withAlpha);

// CSS font-family value for a font face name: the face quoted (quotes and backslashes
// escaped, `;{}<>` and control characters removed, at most 128 bytes) followed by the
// default stack. An empty face gives the default stack alone.
std::string cssFontFamily(const std::string &face);

// The four overlay source types.
enum class SourceKind { Chat, Viewers, Clock, Featured };

// A source setting as OBS stores it: bool, int (colors are 0xAABBGGRR ints) or string.
using SettingValue = std::variant<bool, long long, std::string>;
struct Setting {
	std::string key;
	SettingValue value;
};

// Every setting of a source type with its default value. The OBS glue registers these as the
// source defaults, so this list is also the set of keys a source understands.
std::vector<Setting> defaultSettings(SourceKind kind);

// Value of the "preset" setting that means no preset (the user's own settings).
extern const char *const kCustomPreset;
// Preset ids, in the order the list shows them (without "custom").
std::vector<std::string> presetIds();
// What applying the preset writes for that source type; empty for "custom" or an unknown id.
std::vector<Setting> presetSettings(SourceKind kind, const std::string &id);

// Custom CSS limit, in bytes.
constexpr size_t kCustomCssMax = 20 * 1024;
// Custom CSS for the page's <style id="user-css">: at most kCustomCssMax bytes (never
// splitting a UTF-8 character), without NUL characters and without any "</style" (in any case).
std::string sanitizeCustomCss(const std::string &css);

} // namespace overlay
