// Source settings → the config object each page receives.

#include "source-internal.hpp"

namespace overlay_source {

// Color de una propiedad de OBS (0xAABBGGRR) como "#rrggbb".
QString hexColor(obs_data_t *settings, const char *key)
{
	const auto c = uint32_t(obs_data_get_int(settings, key));
	return QStringLiteral("#%1%2%3")
		.arg(c & 0xff, 2, 16, QLatin1Char('0'))
		.arg((c >> 8) & 0xff, 2, 16, QLatin1Char('0'))
		.arg((c >> 16) & 0xff, 2, 16, QLatin1Char('0'));
}

// Estilo de un fondo (color, opacidad, redondeo y borde). prefix: "" para el fondo de los
// mensajes o del recuadro, "panel_" para el fondo general del chat.
void readBoxStyle(QJsonObject &config, obs_data_t *settings, const std::string &prefix, const QString &js)
{
	const std::string color = prefix.empty() ? "bg_color" : prefix + "color";
	config.insert(js + "Color", hexColor(settings, color.c_str()));
	config.insert(js + "Opacity", (int)obs_data_get_int(settings, (prefix + "opacity").c_str()));
	config.insert(js + "Radius", (int)obs_data_get_int(settings, (prefix + "radius").c_str()));
	config.insert(js + "BorderWidth", (int)obs_data_get_int(settings, (prefix + "border_width").c_str()));
	config.insert(js + "BorderColor", hexColor(settings, (prefix + "border_color").c_str()));
}

void applyDefaults(obs_data_t *settings, Kind kind)
{
	for (const overlay::Setting &setting : overlay::defaultSettings(kind)) {
		const char *key = setting.key.c_str();
		if (const bool *b = std::get_if<bool>(&setting.value))
			obs_data_set_default_bool(settings, key, *b);
		else if (const long long *i = std::get_if<long long>(&setting.value))
			obs_data_set_default_int(settings, key, *i);
		else
			obs_data_set_default_string(settings, key, std::get<std::string>(setting.value).c_str());
	}
}

void applySetting(obs_data_t *settings, const overlay::Setting &setting)
{
	const char *key = setting.key.c_str();
	if (const bool *b = std::get_if<bool>(&setting.value))
		obs_data_set_bool(settings, key, *b);
	else if (const long long *i = std::get_if<long long>(&setting.value))
		obs_data_set_int(settings, key, *i);
	else
		obs_data_set_string(settings, key, std::get<std::string>(setting.value).c_str());
}

bool hasSetting(obs_data_t *settings, const overlay::Setting &setting)
{
	const char *key = setting.key.c_str();
	if (const bool *b = std::get_if<bool>(&setting.value))
		return obs_data_get_bool(settings, key) == *b;
	if (const long long *i = std::get_if<long long>(&setting.value))
		return obs_data_get_int(settings, key) == *i;
	return std::get<std::string>(setting.value) == obs_data_get_string(settings, key);
}

// ---- Typography (text group of every source) ----

// Chat and featured show names and wrap lines; viewers and clock are a single line of numbers.
bool hasNamesAndLines(Kind kind)
{
	return kind == Kind::Chat || kind == Kind::Featured;
}

QString cssColorOf(obs_data_t *settings, const char *key, bool withAlpha)
{
	return QString::fromStdString(overlay::cssColor(uint32_t(obs_data_get_int(settings, key)), withAlpha));
}

void readTextStyle(QJsonObject &config, obs_data_t *settings, Kind kind)
{
	const std::string face = obs_data_get_string(settings, "font_family");
	config.insert("fontFace", QString::fromStdString(face));
	config.insert("font", QString::fromStdString(overlay::cssFontFamily(face)));
	config.insert("textWeight", (int)obs_data_get_int(settings, "font_weight"));
	config.insert("textColor", cssColorOf(settings, "text_color", false));
	config.insert("outlineWidth", (int)obs_data_get_int(settings, "outline_width"));
	config.insert("outlineColor", cssColorOf(settings, "outline_color", false));
	config.insert("shadowMode", obs_data_get_string(settings, "text_shadow"));
	config.insert("shadowColor", cssColorOf(settings, "shadow_color", true));
	config.insert("shadowBlur", (int)obs_data_get_int(settings, "shadow_blur"));
	if (hasNamesAndLines(kind)) {
		config.insert("nameMode", obs_data_get_string(settings, "name_mode"));
		config.insert("nameColor", cssColorOf(settings, "name_color", false));
		config.insert("lineHeight", (int)obs_data_get_int(settings, "line_height"));
	}
}

// Texts the pages show on their own (badge label and sample messages), in OBS's language.
void addPageTexts(QJsonObject &config, Kind kind)
{
	config.insert("memberBadge", i18n::tr("Badge.Member"));
	if (kind == Kind::Featured) {
		config.insert("sampleText", i18n::tr("Featured.SampleText"));
		return;
	}
	QJsonArray samples;
	for (int i = 1; i <= 7; ++i)
		samples.append(i18n::tr(("Overlay.Sample" + std::to_string(i)).c_str()));
	config.insert("samples", samples);
}

QJsonObject readConfig(Kind kind, obs_data_t *settings)
{
	QJsonObject config;
	if (kind == Kind::Featured) {
		config = QJsonObject{
			{"size", (int)obs_data_get_int(settings, "font_size")},
			{"animation", obs_data_get_string(settings, "animation")},
			{"autoHide", obs_data_get_bool(settings, "auto_hide")},
			{"hideBase", (int)obs_data_get_int(settings, "hide_base")},
			{"hidePerChar", (int)obs_data_get_int(settings, "hide_per_char")},
			{"hideMax", (int)obs_data_get_int(settings, "hide_max")},
			{"valign", obs_data_get_string(settings, "valign")},
			{"halign", obs_data_get_string(settings, "halign")},
			{"icons", obs_data_get_bool(settings, "icons")},
			{"badges", obs_data_get_bool(settings, "badges")},
			{"padY", (int)obs_data_get_int(settings, "pad_y")},
			{"padX", (int)obs_data_get_int(settings, "pad_x")},
			{"background", obs_data_get_bool(settings, "background")},
			{"style", obs_data_get_string(settings, "style")},
		};
		readBoxStyle(config, settings, "", "bg");
		readTextStyle(config, settings, kind);
		addPageTexts(config, kind);
		return config;
	}
	QJsonArray platforms;
	for (const platforms::Platform &p : platforms::kAll)
		if (obs_data_get_bool(settings, (std::string("show_") + p.id).c_str()))
			platforms.append(p.id);
	if (kind == Kind::Clock) {
		config = QJsonObject{
			{"size", (int)obs_data_get_int(settings, "font_size")},
			{"date", obs_data_get_bool(settings, "date")},
			{"time", obs_data_get_bool(settings, "time")},
			{"seconds", obs_data_get_bool(settings, "seconds")},
			{"hour24", obs_data_get_bool(settings, "hour24")},
			{"bar", obs_data_get_bool(settings, "bar")},
			{"padY", (int)obs_data_get_int(settings, "pad_y")},
			{"padX", (int)obs_data_get_int(settings, "pad_x")},
			{"background", obs_data_get_bool(settings, "background")},
			{"style", obs_data_get_string(settings, "style")},
		};
		readBoxStyle(config, settings, "", "bg");
		readTextStyle(config, settings, kind);
		return config;
	}
	if (kind == Kind::Viewers) {
		config = QJsonObject{
			{"size", (int)obs_data_get_int(settings, "font_size")},
			{"layout", obs_data_get_string(settings, "layout")},
			{"total", obs_data_get_bool(settings, "total")},
			{"names", obs_data_get_bool(settings, "names")},
			{"padY", (int)obs_data_get_int(settings, "pad_y")},
			{"padX", (int)obs_data_get_int(settings, "pad_x")},
			{"platforms", platforms},
			{"background", obs_data_get_bool(settings, "background")},
			{"style", obs_data_get_string(settings, "style")},
		};
		readBoxStyle(config, settings, "", "bg");
		readTextStyle(config, settings, kind);
		return config;
	}
	config = QJsonObject{
		{"size", (int)obs_data_get_int(settings, "font_size")},
		{"fade", (int)obs_data_get_int(settings, "fade")},
		{"max", (int)obs_data_get_int(settings, "max")},
		{"platforms", platforms},
		{"icons", obs_data_get_bool(settings, "icons")},
		{"badges", obs_data_get_bool(settings, "badges")},
		{"background", obs_data_get_bool(settings, "background")},
		{"panel", obs_data_get_bool(settings, "panel")},
		{"style", obs_data_get_string(settings, "style")},
		{"align", obs_data_get_string(settings, "align")},
		{"layout", obs_data_get_string(settings, "layout")},
		{"speed", (int)obs_data_get_int(settings, "speed")},
		{"spacing", (int)obs_data_get_int(settings, "spacing")},
		{"hspacing", (int)obs_data_get_int(settings, "hspacing")},
		{"padY", (int)obs_data_get_int(settings, "pad_y")},
		{"padX", (int)obs_data_get_int(settings, "pad_x")},
		{"msgPadY", (int)obs_data_get_int(settings, "msg_pad_y")},
		{"msgPadX", (int)obs_data_get_int(settings, "msg_pad_x")},
		{"scroll", obs_data_get_string(settings, "scroll")},
		{"clock", obs_data_get_bool(settings, "clock")},
		{"clockDate", obs_data_get_bool(settings, "clock_date")},
		{"clockTime", obs_data_get_bool(settings, "clock_time")},
		{"clockSeconds", obs_data_get_bool(settings, "clock_seconds")},
		{"clockHour24", obs_data_get_bool(settings, "clock_hour24")},
		{"clockBar", obs_data_get_bool(settings, "clock_bar")},
		{"fit", obs_data_get_bool(settings, "fit_canvas")},
		{"tickerWidth", (int)obs_data_get_int(settings, "ticker_width")},
	};
	readBoxStyle(config, settings, "", "bg");
	readBoxStyle(config, settings, "panel_", "panel");
	readTextStyle(config, settings, kind);
	addPageTexts(config, kind);
	return config;
}

// Id of a featured source in ChatManager's auto-hide registry (the pointer is unique while
// the source lives and ov_destroy withdraws it).
QString featuredSourceId(const OverlaySource *s)
{
	return QString::number(reinterpret_cast<quintptr>(s), 16);
}

// The manager owns the auto-hide timer: it needs the policy of every featured source.
void reportFeaturedPolicy(const OverlaySource *s, obs_data_t *settings)
{
	chat::FeaturedHidePolicy policy;
	policy.enabled = obs_data_get_bool(settings, "auto_hide");
	policy.baseSeconds = int(obs_data_get_int(settings, "hide_base"));
	policy.perCharMs = int(obs_data_get_int(settings, "hide_per_char"));
	policy.maxSeconds = int(obs_data_get_int(settings, "hide_max"));
	ChatManager::instance().setFeaturedAutoHide(featuredSourceId(s), policy);
}

void readSettings(OverlaySource *s, obs_data_t *settings)
{
	if (s->kind == Kind::Featured)
		reportFeaturedPolicy(s, settings);
	// Older scenes: a per-source "preview" set by hand turns the global test messages on, once.
	if (obs_data_has_user_value(settings, "preview"))
		ChatManager::instance().adoptLegacyPreview(obs_data_get_bool(settings, "preview"));
	QJsonObject config = readConfig(s->kind, settings);
	config.insert("customCss",
		      QString::fromStdString(overlay::sanitizeCustomCss(obs_data_get_string(settings, "custom_css"))));
	if (s->kind == Kind::Chat || s->kind == Kind::Featured) { // espectadores y reloj calculan su tamaño solos
		s->width = (uint32_t)std::max<long long>(30, obs_data_get_int(settings, "width"));
		s->height = (uint32_t)std::max<long long>(30, obs_data_get_int(settings, "height"));
	}
	if (fitsCanvas(s->kind, config)) {
		s->width = tickerWidth(config);
		s->height = tickerHeight(config);
	}
	std::lock_guard<std::mutex> lock(s->mutex);
	s->config = config;
}

} // namespace overlay_source
