#include "overlay-presets.hpp"

#include <algorithm>
#include <cstring>

namespace overlay {

const char *const kDefaultFontStack = "\"Segoe UI\", system-ui, -apple-system, \"Helvetica Neue\", Roboto, sans-serif";

namespace {

// Cuts s to at most max bytes without splitting a UTF-8 character.
std::string truncateUtf8(const std::string &s, size_t max)
{
	if (s.size() <= max)
		return s;
	size_t end = max;
	// Back up over continuation bytes (10xxxxxx) to the start of the cut character.
	while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80)
		--end;
	return s.substr(0, end);
}

std::string trim(const std::string &s)
{
	const size_t first = s.find_first_not_of(' ');
	if (first == std::string::npos)
		return {};
	return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

} // namespace

std::string cssColor(uint32_t abgr, bool withAlpha)
{
	const unsigned r = abgr & 0xff, g = (abgr >> 8) & 0xff, b = (abgr >> 16) & 0xff;
	const unsigned a = withAlpha ? (abgr >> 24) & 0xff : 0xff;
	// Thousandths with integers: printf would use the locale's decimal separator.
	const unsigned thousandths = (a * 1000 + 127) / 255;
	std::string alpha;
	if (thousandths >= 1000) {
		alpha = "1";
	} else if (thousandths == 0) {
		alpha = "0";
	} else {
		char digits[4];
		digits[0] = char('0' + thousandths / 100);
		digits[1] = char('0' + thousandths / 10 % 10);
		digits[2] = char('0' + thousandths % 10);
		digits[3] = '\0';
		size_t len = 3;
		while (len > 1 && digits[len - 1] == '0')
			--len;
		alpha = "0." + std::string(digits, len);
	}
	return "rgba(" + std::to_string(r) + ", " + std::to_string(g) + ", " + std::to_string(b) + ", " + alpha + ")";
}

std::string cssFontFamily(const std::string &face)
{
	std::string clean;
	for (char c : face) {
		const auto u = static_cast<unsigned char>(c);
		if (u < 0x20 || u == 0x7f || std::strchr(";{}<>", c))
			continue;
		clean += c;
	}
	clean = trim(truncateUtf8(trim(clean), 128));
	if (clean.empty())
		return kDefaultFontStack;
	std::string quoted = "\"";
	for (char c : clean) {
		if (c == '"' || c == '\\')
			quoted += '\\';
		quoted += c;
	}
	return quoted + "\", " + kDefaultFontStack;
}

const char *const kCustomPreset = "custom";

namespace {

Setting B(const char *key, bool value)
{
	return {key, value};
}
Setting I(const char *key, long long value)
{
	return {key, value};
}
Setting S(const char *key, const char *value)
{
	return {key, std::string(value)};
}

// Background box: prefix "" for the message or box background (color key "bg_color"),
// "panel_" for the chat's overall background.
void addBox(std::vector<Setting> &list, const std::string &prefix, long long opacity, long long radius)
{
	list.push_back({prefix.empty() ? "bg_color" : prefix + "color", 0xFF000000LL});
	list.push_back({prefix + "opacity", opacity});
	list.push_back({prefix + "radius", radius});
	list.push_back({prefix + "border_width", 0LL});
	list.push_back({prefix + "border_color", 0xFFFFFFFFLL});
}

void addPlatforms(std::vector<Setting> &list)
{
	for (const char *p : {"show_twitch", "show_youtube", "show_kick", "show_tiktok", "show_starvios"})
		list.push_back(B(p, true));
}

bool hasNamesAndLines(SourceKind kind)
{
	return kind == SourceKind::Chat || kind == SourceKind::Featured;
}

// Typography, custom CSS and preset: the defaults reproduce the look before these existed.
void addText(std::vector<Setting> &list, SourceKind kind, long long weight, long long lineHeight)
{
	list.push_back(S("font_family", ""));
	list.push_back(I("font_weight", weight));
	list.push_back(I("text_color", 0xFFFFFFFF));
	list.push_back(I("outline_width", 0));
	list.push_back(I("outline_color", 0xFF000000));
	list.push_back(S("text_shadow", "auto"));
	list.push_back(I("shadow_color", 0xFF000000));
	list.push_back(I("shadow_blur", 6));
	if (hasNamesAndLines(kind)) {
		list.push_back(S("name_mode", "user"));
		list.push_back(I("name_color", 0xFFFFFFFF));
		list.push_back(I("line_height", lineHeight));
	}
	list.push_back(S("custom_css", ""));
	list.push_back(S("preset", kCustomPreset));
	list.push_back(S("preset_applied", kCustomPreset));
	list.push_back(B("advanced", false)); // "Show more options" in the properties window
}

// Which source types a preset value applies to.
enum Kinds : unsigned {
	kChat = 1,
	kViewers = 2,
	kClock = 4,
	kFeatured = 8,
	kNamed = kChat | kFeatured,
	kAll = kChat | kViewers | kClock | kFeatured,
};

struct PresetValue {
	unsigned kinds;
	Setting setting;
};

struct Preset {
	const char *id;
	std::vector<PresetValue> values;
};

unsigned bit(SourceKind kind)
{
	switch (kind) {
	case SourceKind::Chat:
		return kChat;
	case SourceKind::Viewers:
		return kViewers;
	case SourceKind::Clock:
		return kClock;
	case SourceKind::Featured:
		return kFeatured;
	}
	return 0;
}

// Colors are OBS ints 0xAABBGGRR (so #ff2bd6 is 0xFFD62BFF).
const std::vector<Preset> &presets()
{
	static const std::vector<Preset> table = {
		// Clean: white text with a soft shadow straight on the scene, no boxes.
		{"clean",
		 {{kAll, B("background", false)},
		  {kAll, S("style", "solid")},
		  {kAll, S("font_family", "")},
		  {kNamed, I("font_weight", 500)},
		  {kAll, I("text_color", 0xFFFFFFFF)},
		  {kNamed, S("name_mode", "user")},
		  {kNamed, I("line_height", 135)},
		  {kAll, I("outline_width", 0)},
		  {kAll, S("text_shadow", "auto")},
		  {kAll, I("shadow_color", 0xFF000000)},
		  {kAll, I("shadow_blur", 6)},
		  {kChat, S("layout", "vertical")},
		  {kChat, B("panel", false)},
		  {kChat, I("font_size", 24)},
		  {kChat, I("spacing", 6)}}},
		// Neon: synthwave boxes, names in the platform color and a magenta glow.
		{"neon",
		 {{kAll, B("background", true)},
		  {kAll, S("style", "synthwave")},
		  {kAll, I("opacity", 70)},
		  {kAll, I("radius", 10)},
		  {kAll, S("font_family", "")},
		  {kNamed, I("font_weight", 600)},
		  {kAll, I("text_color", 0xFFFFF0F5)},
		  {kNamed, S("name_mode", "platform")},
		  {kNamed, I("line_height", 135)},
		  {kAll, I("outline_width", 0)},
		  {kAll, S("text_shadow", "on")},
		  {kAll, I("shadow_color", 0xCCD62BFF)},
		  {kAll, I("shadow_blur", 12)},
		  {kChat, S("layout", "vertical")},
		  {kChat, B("panel", false)},
		  {kChat, I("font_size", 22)},
		  {kChat, I("spacing", 8)},
		  {kChat, I("msg_pad_y", 6)},
		  {kChat, I("msg_pad_x", 12)}}},
		// Bubbles: rounded dark-slate bubbles, no shadow, roomy.
		{"bubbles",
		 {{kAll, B("background", true)},
		  {kAll, S("style", "solid")},
		  {kAll, I("bg_color", 0xFF30221E)},
		  {kAll, I("opacity", 88)},
		  {kAll, I("radius", 18)},
		  {kAll, I("border_width", 0)},
		  {kAll, S("font_family", "")},
		  {kNamed, I("font_weight", 500)},
		  {kAll, I("text_color", 0xFFFFFFFF)},
		  {kNamed, S("name_mode", "user")},
		  {kNamed, I("line_height", 140)},
		  {kAll, I("outline_width", 0)},
		  {kAll, S("text_shadow", "off")},
		  {kChat, S("layout", "vertical")},
		  {kChat, B("panel", false)},
		  {kChat, I("font_size", 22)},
		  {kChat, I("spacing", 8)},
		  {kChat, I("msg_pad_y", 8)},
		  {kChat, I("msg_pad_x", 14)}}},
		// Minimal single line: the chat becomes a ticker over a dark strip with the clock.
		{"minimal",
		 {{kViewers | kClock | kFeatured, B("background", true)},
		  {kChat, B("background", false)},
		  {kAll, S("style", "minimal")},
		  {kAll, I("opacity", 80)},
		  {kAll, I("radius", 6)},
		  {kAll, S("font_family", "")},
		  {kNamed, I("font_weight", 600)},
		  {kAll, I("text_color", 0xFFFFFFFF)},
		  {kNamed, S("name_mode", "user")},
		  {kNamed, I("line_height", 135)},
		  {kAll, I("outline_width", 0)},
		  {kAll, S("text_shadow", "off")},
		  {kChat, S("layout", "horizontal")},
		  {kChat, B("fit_canvas", true)},
		  {kChat, S("scroll", "continuous")},
		  {kChat, B("clock", true)},
		  {kChat, B("panel", true)},
		  {kChat, I("panel_color", 0xFF000000)},
		  {kChat, I("panel_opacity", 55)},
		  {kChat, I("panel_radius", 0)},
		  {kChat, I("panel_border_width", 0)},
		  {kChat, I("font_size", 24)},
		  {kChat, I("hspacing", 48)}}},
		// High contrast: opaque black boxes with a white border, bold text and yellow names.
		{"contrast",
		 {{kAll, B("background", true)},
		  {kAll, S("style", "solid")},
		  {kAll, I("bg_color", 0xFF000000)},
		  {kAll, I("opacity", 100)},
		  {kAll, I("radius", 6)},
		  {kAll, I("border_width", 2)},
		  {kAll, I("border_color", 0xFFFFFFFF)},
		  {kAll, S("font_family", "")},
		  {kNamed, I("font_weight", 700)},
		  {kAll, I("text_color", 0xFFFFFFFF)},
		  {kNamed, S("name_mode", "fixed")},
		  {kNamed, I("name_color", 0xFF00D4FF)},
		  {kNamed, I("line_height", 140)},
		  {kAll, I("outline_width", 0)},
		  {kAll, S("text_shadow", "off")},
		  {kChat, S("layout", "vertical")},
		  {kChat, B("panel", false)},
		  {kChat, I("font_size", 26)},
		  {kChat, I("spacing", 6)}}},
		// Vertical 1080×1920: big glass bubbles for a portrait canvas.
		{"vertical",
		 {{kAll, B("background", true)},
		  {kAll, S("style", "glass")},
		  {kAll, I("bg_color", 0xFF000000)},
		  {kAll, I("opacity", 55)},
		  {kAll, I("radius", 22)},
		  {kAll, S("font_family", "")},
		  {kNamed, I("font_weight", 600)},
		  {kAll, I("text_color", 0xFFFFFFFF)},
		  {kNamed, S("name_mode", "user")},
		  {kNamed, I("line_height", 135)},
		  {kAll, I("outline_width", 0)},
		  {kAll, S("text_shadow", "auto")},
		  {kChat, S("layout", "vertical")},
		  {kChat, B("panel", false)},
		  {kChat, S("align", "bottom")},
		  {kChat, I("width", 1000)},
		  {kChat, I("height", 1400)},
		  {kChat, I("font_size", 34)},
		  {kChat, I("max", 8)},
		  {kChat, I("spacing", 10)},
		  {kChat, I("msg_pad_y", 10)},
		  {kChat, I("msg_pad_x", 16)},
		  {kFeatured, I("width", 1000)},
		  {kFeatured, I("height", 420)},
		  {kFeatured, I("font_size", 44)}}},
	};
	return table;
}

} // namespace

std::vector<Setting> defaultSettings(SourceKind kind)
{
	std::vector<Setting> list;
	switch (kind) {
	case SourceKind::Chat:
		list = {I("width", 500),         I("height", 600),           I("font_size", 22),
			I("fade", 0),            I("max", 15),               B("icons", true),
			B("badges", true),       B("background", false),     S("style", "solid"),
			B("panel", false),       S("align", "bottom"),       S("layout", "vertical"),
			I("speed", 120),         S("scroll", "continuous"),  B("clock", false),
			B("clock_date", true),   B("clock_time", true),      B("clock_seconds", false),
			B("clock_hour24", true), B("clock_bar", true),       B("fit_canvas", true),
			I("ticker_width", 0),    I("spacing", 5),            I("hspacing", 48),
			I("pad_y", 6),           I("pad_x", 10),             I("msg_pad_y", 5),
			I("msg_pad_x", 11),      B("preview", true), // preview: legacy, see adoptLegacyPreview
			S("clock_position", "top")}; // vertical list only; the ticker keeps it at its end
		addPlatforms(list);
		addBox(list, "", 60, 9);
		addBox(list, "panel_", 50, 0);
		addText(list, kind, 500, 135);
		break;
	case SourceKind::Viewers:
		list = {I("font_size", 28), S("layout", "row"), B("total", true),       B("names", false),
			I("pad_y", 10),     I("pad_x", 17),     B("background", true), S("style", "solid"),
			B("preview", true)};
		addPlatforms(list);
		addBox(list, "", 60, 12);
		addText(list, kind, 700, 120);
		break;
	case SourceKind::Clock:
		list = {I("font_size", 32), B("date", true), B("time", true),       B("seconds", false),
			B("hour24", true),  B("bar", true),  I("pad_y", 11),        I("pad_x", 19),
			B("background", true), S("style", "solid")};
		addBox(list, "", 60, 12);
		addText(list, kind, 800, 120);
		break;
	case SourceKind::Featured:
		list = {I("width", 900),          I("height", 320),        I("font_size", 34),
			S("animation", "slide"),  B("auto_hide", true),    I("hide_base", 8),
			I("hide_per_char", 60),   I("hide_max", 30),       S("valign", "bottom"),
			S("halign", "center"),    B("icons", true),        B("badges", true),
			I("pad_y", 16),           I("pad_x", 22),          B("background", true),
			S("style", "solid"),      B("preview", true)};
		addBox(list, "", 75, 16);
		addText(list, kind, 500, 130);
		break;
	}
	return list;
}

std::vector<std::string> presetIds()
{
	std::vector<std::string> ids;
	for (const Preset &preset : presets())
		ids.emplace_back(preset.id);
	return ids;
}

std::vector<Setting> presetSettings(SourceKind kind, const std::string &id)
{
	std::vector<Setting> list;
	for (const Preset &preset : presets()) {
		if (id != preset.id)
			continue;
		for (const PresetValue &value : preset.values)
			if (value.kinds & bit(kind))
				list.push_back(value.setting);
	}
	return list;
}

std::string sanitizeCustomCss(const std::string &css)
{
	static const char closing[] = "</style";
	const size_t n = sizeof(closing) - 1;
	std::string out;
	out.reserve(std::min(css.size(), kCustomCssMax));
	// One pass: whenever the output ends in "</style" (any case) it is dropped, so a removal
	// that joins the text around it into a new "</style" is caught too.
	for (char c : truncateUtf8(css, kCustomCssMax)) {
		if (c == '\0')
			continue;
		out += c;
		if (out.size() < n)
			continue;
		bool match = true;
		for (size_t i = 0; i < n && match; ++i) {
			char o = out[out.size() - n + i];
			if (o >= 'A' && o <= 'Z')
				o = char(o - 'A' + 'a');
			match = o == closing[i];
		}
		if (match)
			out.erase(out.size() - n);
	}
	return out;
}

} // namespace overlay
