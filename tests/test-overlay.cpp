// Tests for the pure overlay helpers (CSS values from OBS settings), without OBS.

#include "../src/overlay-presets.hpp"
#include "../src/platforms-registry.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QtTest/QtTest>

#include <map>
#include <set>
#include <string>

using namespace overlay;

namespace {

const SourceKind kKinds[] = {SourceKind::Chat, SourceKind::Viewers, SourceKind::Clock, SourceKind::Featured};

std::map<std::string, SettingValue> asMap(const std::vector<Setting> &list)
{
	std::map<std::string, SettingValue> map;
	for (const Setting &s : list)
		map.emplace(s.key, s.value);
	return map;
}

using V = SettingValue;
V I(long long v)
{
	return V(v);
}
V S(const char *v)
{
	return V(std::string(v));
}

// Background box keys with the defaults every version had: black, white border, no border.
void addBox(std::map<std::string, V> &m, const std::string &prefix, long long opacity, long long radius)
{
	m[prefix.empty() ? "bg_color" : prefix + "color"] = I(0xFF000000);
	m[prefix + "opacity"] = I(opacity);
	m[prefix + "radius"] = I(radius);
	m[prefix + "border_width"] = I(0);
	m[prefix + "border_color"] = I(0xFFFFFFFF);
}

void addPlatforms(std::map<std::string, V> &m)
{
	for (const char *p : {"show_twitch", "show_youtube", "show_kick", "show_tiktok", "show_starvios"})
		m[p] = V(true);
}

// Keys and defaults of the sources before block 4: saved scenes rely on them.
std::map<std::string, V> legacyDefaults(SourceKind kind)
{
	std::map<std::string, V> m;
	switch (kind) {
	case SourceKind::Chat:
		m = {{"width", I(500)},          {"height", I(600)},           {"font_size", I(22)},
		     {"fade", I(0)},             {"max", I(15)},               {"icons", V(true)},
		     {"badges", V(true)},        {"background", V(false)},     {"style", S("solid")},
		     {"panel", V(false)},        {"align", S("bottom")},       {"layout", S("vertical")},
		     {"speed", I(120)},          {"scroll", S("continuous")},  {"clock", V(false)},
		     {"clock_date", V(true)},    {"clock_time", V(true)},      {"clock_seconds", V(false)},
		     {"clock_hour24", V(true)},  {"clock_bar", V(true)},       {"fit_canvas", V(true)},
		     {"ticker_width", I(0)},     {"spacing", I(5)},            {"hspacing", I(48)},
		     {"pad_y", I(6)},            {"pad_x", I(10)},             {"msg_pad_y", I(5)},
		     {"msg_pad_x", I(11)},       {"preview", V(true)}};
		addPlatforms(m);
		addBox(m, "", 60, 9);
		addBox(m, "panel_", 50, 0);
		break;
	case SourceKind::Viewers:
		m = {{"font_size", I(28)}, {"layout", S("row")}, {"total", V(true)},       {"names", V(false)},
		     {"pad_y", I(10)},     {"pad_x", I(17)},     {"background", V(true)}, {"style", S("solid")},
		     {"preview", V(true)}};
		addPlatforms(m);
		addBox(m, "", 60, 12);
		break;
	case SourceKind::Clock:
		m = {{"font_size", I(32)}, {"date", V(true)}, {"time", V(true)},       {"seconds", V(false)},
		     {"hour24", V(true)},  {"bar", V(true)},  {"pad_y", I(11)},        {"pad_x", I(19)},
		     {"background", V(true)}, {"style", S("solid")}};
		addBox(m, "", 60, 12);
		break;
	case SourceKind::Featured:
		m = {{"width", I(900)},         {"height", I(320)},       {"font_size", I(34)},
		     {"animation", S("slide")}, {"auto_hide", V(true)},   {"hide_base", I(8)},
		     {"hide_per_char", I(60)},  {"hide_max", I(30)},      {"valign", S("bottom")},
		     {"halign", S("center")},   {"icons", V(true)},       {"badges", V(true)},
		     {"pad_y", I(16)},          {"pad_x", I(22)},         {"background", V(true)},
		     {"style", S("solid")},     {"preview", V(true)}};
		addBox(m, "", 75, 16);
		break;
	}
	// Block 6 (simplify-ux): the "Show advanced options" switch of every source starts off.
	m["advanced"] = V(false);
	return m;
}

const char *kindName(SourceKind kind)
{
	switch (kind) {
	case SourceKind::Chat:
		return "chat";
	case SourceKind::Viewers:
		return "viewers";
	case SourceKind::Clock:
		return "clock";
	case SourceKind::Featured:
		return "featured";
	}
	return "?";
}

} // namespace

class OverlayTest : public QObject {
	Q_OBJECT

private slots:
	void colorWithoutAlphaIsOpaque()
	{
		// OBS stores 0xAABBGGRR: red is the low byte.
		QCOMPARE(cssColor(0xFF0000FFu, false), std::string("rgba(255, 0, 0, 1)"));
		QCOMPARE(cssColor(0x00FF8040u, false), std::string("rgba(64, 128, 255, 1)"));
	}

	void colorWithAlpha()
	{
		QCOMPARE(cssColor(0x80FF8040u, true), std::string("rgba(64, 128, 255, 0.502)"));
		QCOMPARE(cssColor(0xFF000000u, true), std::string("rgba(0, 0, 0, 1)"));
		QCOMPARE(cssColor(0x00000000u, true), std::string("rgba(0, 0, 0, 0)"));
		QCOMPARE(cssColor(0x33FFFFFFu, true), std::string("rgba(255, 255, 255, 0.2)"));
	}

	void emptyFontGivesDefaultStack()
	{
		QCOMPARE(cssFontFamily(""), std::string(kDefaultFontStack));
		QCOMPARE(cssFontFamily("   "), std::string(kDefaultFontStack));
	}

	void fontIsQuotedBeforeTheStack()
	{
		QCOMPARE(cssFontFamily("Inter"), "\"Inter\", " + std::string(kDefaultFontStack));
		QCOMPARE(cssFontFamily("  Noto Sans 日本語 "), "\"Noto Sans 日本語\", " + std::string(kDefaultFontStack));
	}

	void fontCannotBreakOutOfTheValue()
	{
		const std::string css = cssFontFamily("Bad\"; } body { color: red } <x>\\");
		const std::string face = css.substr(0, css.size() - std::string(kDefaultFontStack).size());
		for (char c : std::string(";{}<>"))
			QVERIFY2(face.find(c) == std::string::npos, face.c_str());
		QCOMPARE(face, std::string("\"Bad\\\"  body  color: red  x\\\\\", "));
	}

	void fontDropsControlCharactersAndIsCapped()
	{
		QCOMPARE(cssFontFamily("A\nB\tC"), "\"ABC\", " + std::string(kDefaultFontStack));
		const std::string css = cssFontFamily(std::string(500, 'x'));
		QCOMPARE(css, "\"" + std::string(128, 'x') + "\", " + std::string(kDefaultFontStack));
		// The cap never splits a UTF-8 character: 127 x + "é" (2 bytes) keeps only the x.
		const std::string cut = cssFontFamily(std::string(127, 'x') + "é");
		QCOMPARE(cut, "\"" + std::string(127, 'x') + "\", " + std::string(kDefaultFontStack));
	}

	void legacyKeysKeepTheirDefaults()
	{
		for (SourceKind kind : kKinds) {
			const auto defaults = asMap(defaultSettings(kind));
			for (const auto &[key, value] : legacyDefaults(kind)) {
				const std::string where = std::string(kindName(kind)) + ": " + key;
				QVERIFY2(defaults.count(key), where.c_str());
				QVERIFY2(defaults.at(key) == value, where.c_str());
			}
		}
	}

	void newTextDefaultsKeepThePreviousLook()
	{
		const std::map<SourceKind, long long> weights = {{SourceKind::Chat, 500},
								 {SourceKind::Viewers, 700},
								 {SourceKind::Clock, 800},
								 {SourceKind::Featured, 500}};
		for (SourceKind kind : kKinds) {
			const auto d = asMap(defaultSettings(kind));
			QVERIFY(d.at("font_family") == S(""));
			QVERIFY(d.at("font_weight") == I(weights.at(kind)));
			QVERIFY(d.at("text_color") == I(0xFFFFFFFF));
			QVERIFY(d.at("outline_width") == I(0));
			QVERIFY(d.at("text_shadow") == S("auto"));
			QVERIFY(d.at("shadow_color") == I(0xFF000000));
			QVERIFY(d.at("shadow_blur") == I(6));
			QVERIFY(d.at("custom_css") == S(""));
			QVERIFY(d.at("preset") == S(kCustomPreset));
		}
		QVERIFY(asMap(defaultSettings(SourceKind::Chat)).at("line_height") == I(135));
		QVERIFY(asMap(defaultSettings(SourceKind::Featured)).at("line_height") == I(130));
		QVERIFY(asMap(defaultSettings(SourceKind::Chat)).at("name_mode") == S("user"));
		QVERIFY(!asMap(defaultSettings(SourceKind::Clock)).count("name_mode"));
	}

	// The chat's own clock works in both layouts: at the end of the ticker, or above or below
	// the vertical list. It starts off, so saved scenes look the same.
	void chatClockHasAVerticalPosition()
	{
		const auto chat = asMap(defaultSettings(SourceKind::Chat));
		QVERIFY(chat.at("clock") == V(false));
		QVERIFY2(chat.count("clock_position"), "clock_position");
		QVERIFY(chat.at("clock_position") == S("top"));
		for (SourceKind kind : {SourceKind::Viewers, SourceKind::Clock, SourceKind::Featured})
			QVERIFY(!asMap(defaultSettings(kind)).count("clock_position"));
	}

	void defaultsHaveNoDuplicateKeys()
	{
		for (SourceKind kind : kKinds) {
			const auto list = defaultSettings(kind);
			QCOMPARE(asMap(list).size(), list.size());
		}
	}

	void thereAreSixPresets()
	{
		const auto ids = presetIds();
		QCOMPARE(ids.size(), size_t(6));
		QCOMPARE(std::set<std::string>(ids.begin(), ids.end()).size(), ids.size());
		for (const std::string &id : ids)
			QVERIFY(id != kCustomPreset);
		QVERIFY(presetSettings(SourceKind::Chat, kCustomPreset).empty());
		QVERIFY(presetSettings(SourceKind::Chat, "nope").empty());
	}

	void presetsOnlyUseKnownKeysWithTheirType()
	{
		for (SourceKind kind : kKinds) {
			const auto defaults = asMap(defaultSettings(kind));
			for (const std::string &id : presetIds()) {
				const auto values = presetSettings(kind, id);
				// Every preset changes the look of every source type.
				QVERIFY2(values.size() >= 8, (id + " " + kindName(kind)).c_str());
				QCOMPARE(asMap(values).size(), values.size());
				for (const Setting &s : values) {
					const std::string where = id + " " + kindName(kind) + ": " + s.key;
					QVERIFY2(defaults.count(s.key), where.c_str());
					QVERIFY2(defaults.at(s.key).index() == s.value.index(), where.c_str());
					QVERIFY2(s.key != "preset" && s.key != "custom_css", where.c_str());
				}
			}
		}
	}

	void presetsLookDifferent()
	{
		std::set<std::map<std::string, SettingValue>> looks;
		for (const std::string &id : presetIds())
			looks.insert(asMap(presetSettings(SourceKind::Chat, id)));
		QCOMPARE(looks.size(), presetIds().size());
		// The single-line preset turns the chat into the ticker; the vertical one is tall.
		QVERIFY(asMap(presetSettings(SourceKind::Chat, "minimal")).at("layout") == S("horizontal"));
		const auto vertical = asMap(presetSettings(SourceKind::Chat, "vertical"));
		QVERIFY(std::get<long long>(vertical.at("height")) > std::get<long long>(vertical.at("width")));
	}

	void customCssIsKept()
	{
		const std::string css = ".msg { color: red; }\n/* é */ .author::before { content: \"> \"; }";
		QCOMPARE(sanitizeCustomCss(css), css);
		QCOMPARE(sanitizeCustomCss(""), std::string());
	}

	void customCssCannotCloseTheStyle()
	{
		QCOMPARE(sanitizeCustomCss("a{}</style><script>x()</script>"), std::string("a{}><script>x()</script>"));
		QCOMPARE(sanitizeCustomCss("a{}</STYLE >"), std::string("a{} >"));
		// Removing one occurrence must not leave another one behind.
		QCOMPARE(sanitizeCustomCss("</sty</stylele"), std::string());
		QCOMPARE(sanitizeCustomCss(std::string("a\0b", 3)), std::string("ab"));
	}

	void customCssIsCapped()
	{
		QCOMPARE(sanitizeCustomCss(std::string(kCustomCssMax + 50, 'a')).size(), kCustomCssMax);
		const std::string cut = sanitizeCustomCss(std::string(kCustomCssMax - 1, 'a') + "é");
		QCOMPARE(cut, std::string(kCustomCssMax - 1, 'a'));
	}

	void platformRegistryOrderAndNames()
	{
		QCOMPARE(platforms::order("twitch"), 0);
		QCOMPARE(platforms::order("starvios"), 4);
		QCOMPARE(platforms::order("demo"), 5);
		QCOMPARE(platforms::order("nope"), -1);
		QCOMPARE(QString(platforms::find("youtube")->name), QString("YouTube"));
		QCOMPARE(platforms::find("demo")->color, platforms::kUnknownColor);
		QVERIFY(!platforms::find("nope"));
	}

	// The pages keep their own table in icons.js: same ids, order and colors as the C++ registry.
	void iconsJsMatchesTheRegistry()
	{
		QFile file(QStringLiteral(MULTICHAT_DATA_DIR "/overlay/icons.js"));
		QVERIFY(file.open(QIODevice::ReadOnly));
		const QString js = QString::fromUtf8(file.readAll());
		const QRegularExpressionMatch table =
			QRegularExpression("const PLATFORMS = \\{([^}]*)\\}").match(js);
		QVERIFY(table.hasMatch());
		QStringList found;
		auto it = QRegularExpression("(\\w+):\\s*'#([0-9A-Fa-f]{6})'").globalMatch(table.captured(1));
		while (it.hasNext()) {
			const QRegularExpressionMatch m = it.next();
			found << m.captured(1) + "=" + m.captured(2).toUpper();
		}
		QStringList expected;
		for (const platforms::Platform &p : platforms::kAll)
			expected << QLatin1String(p.id) + "=" + QString("%1").arg(p.color, 6, 16, QChar('0')).toUpper();
		QCOMPARE(found, expected);
	}
};

QTEST_GUILESS_MAIN(OverlayTest)
#include "test-overlay.moc"
