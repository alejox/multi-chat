// The properties window of each Multichat source.

#include "source-internal.hpp"

namespace overlay_source {

void addBoxStyle(obs_properties_t *props, const std::string &prefix)
{
	// Textos "Style.*" para el fondo de los mensajes o del recuadro, "Panel.*" para el general.
	const std::string group = prefix.empty() ? "Style." : "Panel.";
	auto text = [&](const char *name) { return obs_module_text((group + name).c_str()); };
	const std::string color = prefix.empty() ? "bg_color" : prefix + "color";
	obs_properties_add_color(props, color.c_str(), text("Color"));
	obs_property_t *opacity =
		obs_properties_add_int_slider(props, (prefix + "opacity").c_str(), text("Opacity"), 0, 100, 5);
	obs_property_int_set_suffix(opacity, " %");
	obs_property_t *radius =
		obs_properties_add_int_slider(props, (prefix + "radius").c_str(), text("Radius"), 0, 100, 1);
	obs_property_int_set_suffix(radius, " px");
	obs_property_t *border =
		obs_properties_add_int_slider(props, (prefix + "border_width").c_str(), text("Border"), 0, 20, 1);
	obs_property_int_set_suffix(border, " px");
	obs_properties_add_color(props, (prefix + "border_color").c_str(), text("BorderColor"));
}

// Relleno arriba/abajo e izquierda/derecha.
void addPadding(obs_properties_t *props, const char *keyY, const char *keyX, const char *textY, const char *textX)
{
	obs_property_t *y = obs_properties_add_int_slider(props, keyY, obs_module_text(textY), 0, 100, 1);
	obs_property_int_set_suffix(y, " px");
	obs_property_t *x = obs_properties_add_int_slider(props, keyX, obs_module_text(textX), 0, 200, 1);
	obs_property_int_set_suffix(x, " px");
}

// Estilos de fondo de data/overlay/styles.css (los mismos que music-now-playing).
void addStyleList(obs_properties_t *props)
{
	obs_property_t *style = obs_properties_add_list(props, "style", obs_module_text("Style"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const char *id : {"solid", "glass", "pill", "macos", "minimal", "line", "synthwave", "vice", "neoncity", "cyber",
			       "retro", "glitch"})
		obs_property_list_add_string(style, obs_module_text(("Style." + std::string(id)).c_str()), id);
	obs_property_set_long_description(style, obs_module_text("Style.Help"));
}

// Solo se ven los controles que afectan al estilo elegido: el color en Sólido y en los de
// cristal (es su tinte), el redondeo salvo en Píldora y Cyberpunk, y el borde en Sólido.
void showBoxStyle(obs_properties_t *props, obs_data_t *settings, const std::string &prefix, bool visible)
{
	const std::string style = obs_data_get_string(settings, "style");
	const bool tinted = style == "solid" || style == "glass" || style == "pill" || style == "macos";
	const bool rounded = style != "pill" && style != "cyber";
	const std::string color = prefix.empty() ? "bg_color" : prefix + "color";
	auto show = [&](const std::string &key, bool on) {
		obs_property_set_visible(obs_properties_get(props, key.c_str()), visible && on);
	};
	show(color, tinted);
	show(prefix + "opacity", true);
	show(prefix + "radius", rounded);
	show(prefix + "border_width", style == "solid");
	show(prefix + "border_color", style == "solid");
}

// ---- Settings table (defaults and presets live in overlay-presets.cpp) ----

void addTextStyle(obs_properties_t *props, Kind kind)
{
	obs_property_t *family = obs_properties_add_list(props, "font_family", obs_module_text("Text.Font"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(family, obs_module_text("Text.Font.Default"), "");
	for (const QString &name : QFontDatabase::families()) {
		if (name.startsWith('.') || QFontDatabase::isPrivateFamily(name))
			continue;
		const QByteArray utf8 = name.toUtf8();
		obs_property_list_add_string(family, utf8.constData(), utf8.constData());
	}
	obs_property_set_long_description(family, obs_module_text("Text.Font.Help"));

	obs_property_t *weight = obs_properties_add_list(props, "font_weight", obs_module_text("Text.Weight"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int w : {300, 400, 500, 600, 700, 800, 900})
		obs_property_list_add_int(weight, obs_module_text(("Text.Weight." + std::to_string(w)).c_str()), w);
	obs_property_set_long_description(
		weight, obs_module_text(hasNamesAndLines(kind) ? "Text.Weight.Help" : "Text.Weight.HelpBox"));

	obs_properties_add_color(props, "text_color", obs_module_text("Text.Color"));
	if (hasNamesAndLines(kind)) {
		obs_property_t *mode = obs_properties_add_list(props, "name_mode", obs_module_text("Text.NameMode"),
							       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		for (const char *id : {"user", "platform", "fixed"})
			obs_property_list_add_string(mode, obs_module_text(("Text.NameMode." + std::string(id)).c_str()), id);
		obs_properties_add_color(props, "name_color", obs_module_text("Text.NameColor"));
	}

	obs_property_t *outline =
		obs_properties_add_int_slider(props, "outline_width", obs_module_text("Text.Outline"), 0, 10, 1);
	obs_property_int_set_suffix(outline, " px");
	obs_properties_add_color(props, "outline_color", obs_module_text("Text.OutlineColor"));

	obs_property_t *shadow = obs_properties_add_list(props, "text_shadow", obs_module_text("Text.Shadow"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const char *id : {"auto", "on", "off"})
		obs_property_list_add_string(shadow, obs_module_text(("Text.Shadow." + std::string(id)).c_str()), id);
	obs_property_set_long_description(shadow, obs_module_text("Text.Shadow.Help"));
	obs_properties_add_color_alpha(props, "shadow_color", obs_module_text("Text.ShadowColor"));
	obs_property_t *blur =
		obs_properties_add_int_slider(props, "shadow_blur", obs_module_text("Text.ShadowBlur"), 0, 30, 1);
	obs_property_int_set_suffix(blur, " px");
}

// Line height, only for sources with names and wrapped lines (chat and featured). It lives with
// the rarely changed options, apart from the rest of the text style.
void addLineHeight(obs_properties_t *props, Kind kind)
{
	if (!hasNamesAndLines(kind))
		return;
	obs_property_t *lines =
		obs_properties_add_int_slider(props, "line_height", obs_module_text("Text.LineHeight"), 80, 250, 5);
	obs_property_int_set_suffix(lines, " %");
}

// Controls that only matter with another option: the fixed name color, the outline color and
// the shadow color and blur.
void showTextStyle(obs_properties_t *props, obs_data_t *settings)
{
	obs_property_set_visible(obs_properties_get(props, "name_color"),
				 strcmp(obs_data_get_string(settings, "name_mode"), "fixed") == 0);
	obs_property_set_visible(obs_properties_get(props, "outline_color"),
				 obs_data_get_int(settings, "outline_width") > 0);
	const bool shadow = strcmp(obs_data_get_string(settings, "text_shadow"), "off") != 0;
	obs_property_set_visible(obs_properties_get(props, "shadow_color"), shadow);
	obs_property_set_visible(obs_properties_get(props, "shadow_blur"), shadow);
}

// The "More options" group only shows with "Show more options"; the presets cover most cases
// without it.
void showAdvancedGroups(obs_properties_t *props, obs_data_t *settings)
{
	if (obs_property_t *p = obs_properties_get(props, "group_advanced"))
		obs_property_set_visible(p, obs_data_get_bool(settings, "advanced"));
}

// Los controles del fondo solo se ven con su casilla activada.
bool box_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	showBoxStyle(props, settings, "", obs_data_get_bool(settings, "background"));
	obs_property_set_visible(obs_properties_get(props, "style"), obs_data_get_bool(settings, "background"));
	showTextStyle(props, settings);
	showAdvancedGroups(props, settings);
	return true;
}

// En horizontal no se usan "apilar", "ocultar después de", el máximo ni el tamaño (es automático);
// la velocidad solo en horizontal.
bool ov_layout_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool horizontal = strcmp(obs_data_get_string(settings, "layout"), "horizontal") == 0;
	// Versiones de prueba anteriores guardaban aquí el tamaño vertical al pasar a horizontal.
	if (!horizontal && obs_data_has_user_value(settings, "vertical_width")) {
		obs_data_set_int(settings, "width", obs_data_get_int(settings, "vertical_width"));
		obs_data_set_int(settings, "height", obs_data_get_int(settings, "vertical_height"));
		for (const char *key : {"vertical_width", "vertical_height", "fitted"})
			obs_data_erase(settings, key);
	}
	const bool manualSize = !horizontal || !obs_data_get_bool(settings, "fit_canvas");
	obs_property_set_visible(obs_properties_get(props, "width"), manualSize);
	obs_property_set_visible(obs_properties_get(props, "height"), manualSize);
	obs_property_set_visible(obs_properties_get(props, "fit_canvas"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "ticker_width"), horizontal && !manualSize);
	obs_property_set_visible(obs_properties_get(props, "scroll"), horizontal);
	// The clock works in both layouts: at the end of the ticker, or above or below the list.
	const bool clockOn = obs_data_get_bool(settings, "clock");
	for (const char *key : {"clock_date", "clock_time", "clock_seconds", "clock_hour24", "clock_bar"})
		obs_property_set_visible(obs_properties_get(props, key), clockOn);
	obs_property_set_visible(obs_properties_get(props, "clock_position"), clockOn && !horizontal);
	obs_property_set_visible(obs_properties_get(props, "speed"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "hspacing"), horizontal);
	obs_property_set_visible(obs_properties_get(props, "spacing"), !horizontal);
	obs_property_set_visible(obs_properties_get(props, "align"), !horizontal);
	// Disappearing and the message limit only apply to the vertical list.
	obs_property_set_visible(obs_properties_get(props, "fade"), !horizontal);
	obs_property_set_visible(obs_properties_get(props, "max"), !horizontal);
	showBoxStyle(props, settings, "", obs_data_get_bool(settings, "background"));
	showBoxStyle(props, settings, "panel_", obs_data_get_bool(settings, "panel"));
	obs_property_set_visible(obs_properties_get(props, "style"),
				 obs_data_get_bool(settings, "background") || obs_data_get_bool(settings, "panel"));
	for (const char *key : {"msg_pad_y", "msg_pad_x"})
		obs_property_set_visible(obs_properties_get(props, key), obs_data_get_bool(settings, "background"));
	showTextStyle(props, settings);
	showAdvancedGroups(props, settings);
	return true;
}

bool fe_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);

// ---- Presets, custom CSS and the shared modified callback ----

void *kindParam(Kind kind)
{
	return reinterpret_cast<void *>(static_cast<intptr_t>(kind));
}

void addPreset(obs_properties_t *props)
{
	obs_property_t *preset = obs_properties_add_list(props, "preset", obs_module_text("Preset"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(preset, obs_module_text("Preset.custom"), overlay::kCustomPreset);
	for (const std::string &id : overlay::presetIds())
		obs_property_list_add_string(preset, obs_module_text(("Preset." + id).c_str()), id.c_str());
	obs_property_set_long_description(preset, obs_module_text("Preset.Help"));
}

void addCustomCss(obs_properties_t *props)
{
	obs_property_t *css =
		obs_properties_add_text(props, "custom_css", obs_module_text("Advanced.CustomCss"), OBS_TEXT_MULTILINE);
	obs_property_set_long_description(css, obs_module_text("Advanced.CustomCss.Help"));
}

// Properties whose value shows or hides others: changing them refreshes the window. Any other
// change only refreshes when it takes the source off its preset, so typing in a text box or
// dragging a slider is not interrupted.
bool changesVisibility(const char *name)
{
	for (const char *key : {"layout", "fit_canvas", "clock", "style", "background", "panel", "auto_hide",
				"name_mode", "outline_width", "text_shadow", "advanced"})
		if (strcmp(name, key) == 0)
			return true;
	return false;
}

bool on_modified(void *param, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	const auto kind = static_cast<Kind>(reinterpret_cast<intptr_t>(param));
	auto updateVisibility = [&] {
		if (kind == Kind::Chat)
			ov_layout_changed(props, property, settings);
		else if (kind == Kind::Featured)
			fe_changed(props, property, settings);
		else
			box_changed(props, property, settings);
	};
	const char *name = obs_property_name(property);
	const std::string preset = obs_data_get_string(settings, "preset");
	if (strcmp(name, "preset") == 0) {
		// OBS calls this again every time the window opens or refreshes: the preset is written
		// only when a different one is chosen, so it never undoes later changes.
		if (preset == obs_data_get_string(settings, "preset_applied"))
			return false;
		for (const overlay::Setting &setting : overlay::presetSettings(kind, preset))
			applySetting(settings, setting);
		obs_data_set_string(settings, "preset_applied", preset.c_str());
		updateVisibility();
		return true;
	}
	updateVisibility();
	bool refresh = changesVisibility(name);
	// Back to "Custom" as soon as a value the preset wrote is changed.
	if (preset != overlay::kCustomPreset) {
		for (const overlay::Setting &setting : overlay::presetSettings(kind, preset)) {
			if (hasSetting(settings, setting))
				continue;
			obs_data_set_string(settings, "preset", overlay::kCustomPreset);
			obs_data_set_string(settings, "preset_applied", overlay::kCustomPreset);
			refresh = true;
			break;
		}
	}
	return refresh;
}

// Every property goes through on_modified (visibility and the preset).
void hookAll(obs_properties_t *props, Kind kind)
{
	for (obs_property_t *p = obs_properties_first(props); p; obs_property_next(&p)) {
		const obs_property_type type = obs_property_get_type(p);
		if (type == OBS_PROPERTY_GROUP) {
			hookAll(obs_property_group_content(p), kind);
			continue;
		}
		if (type == OBS_PROPERTY_BUTTON)
			continue;
		obs_property_set_modified_callback2(p, on_modified, kindParam(kind));
	}
}

bool ov_open_settings(obs_properties_t *, obs_property_t *, void *)
{
	open_multichat_settings_from_source();
	return false;
}

// Sections of the properties window. The settings inside a group stay flat in the same
// obs_data, so the keys saved by older versions keep working.
obs_properties_t *addGroup(obs_properties_t *props, const char *name, const char *text)
{
	obs_properties_t *group = obs_properties_create();
	obs_properties_add_group(props, name, obs_module_text(text), OBS_GROUP_NORMAL, group);
	return group;
}

// The switch that reveals the "More options" group; it sits right before that group.
void addAdvancedSwitch(obs_properties_t *props)
{
	obs_property_t *advanced = obs_properties_add_bool(props, "advanced", obs_module_text("Source.Advanced"));
	obs_property_set_long_description(advanced, obs_module_text("Source.Advanced.Help"));
}

void addPlatforms(obs_properties_t *props)
{
	obs_properties_add_bool(props, "show_twitch", obs_module_text("Source.ShowTwitch"));
	obs_properties_add_bool(props, "show_youtube", obs_module_text("Source.ShowYouTube"));
	obs_properties_add_bool(props, "show_kick", obs_module_text("Source.ShowKick"));
	obs_properties_add_bool(props, "show_tiktok", obs_module_text("Source.ShowTikTok"));
	obs_properties_add_bool(props, "show_starvios", obs_module_text("Source.ShowStarvios"));
}

// Chat source, ordered by what users change most: style, background, layout, date and time,
// and the rarely used options last, behind "Show more options".
obs_properties_t *ov_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_button2(props, "chats", obs_module_text("Source.Chats"), ov_open_settings, nullptr);

	obs_properties_t *style = addGroup(props, "group_style", "Group.Style");
	addPreset(style);
	obs_property_t *size =
		obs_properties_add_int_slider(style, "font_size", obs_module_text("Source.FontSize"), 10, 72, 1);
	obs_property_int_set_suffix(size, " px");
	addTextStyle(style, Kind::Chat);

	// Both backgrounds in one place: the style list applies to whichever is on, then the box
	// behind each message and the panel behind the whole chat, each shown with its checkbox.
	obs_properties_t *background = addGroup(props, "group_background", "Group.Background");
	addStyleList(background);
	obs_properties_add_bool(background, "background", obs_module_text("Source.Background"));
	addBoxStyle(background, "");
	addPadding(background, "msg_pad_y", "msg_pad_x", "Source.MsgPadY", "Source.MsgPadX");
	obs_properties_add_bool(background, "panel", obs_module_text("Source.Panel"));
	addBoxStyle(background, "panel_");

	obs_properties_t *design = addGroup(props, "group_layout", "Group.Layout");
	obs_property_t *layout = obs_properties_add_list(design, "layout", obs_module_text("Source.Layout"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(layout, obs_module_text("Source.Layout.Vertical"), "vertical");
	obs_property_list_add_string(layout, obs_module_text("Source.Layout.Horizontal"), "horizontal");
	obs_property_set_long_description(layout, obs_module_text("Source.Layout.Help"));
	obs_property_t *fit = obs_properties_add_bool(design, "fit_canvas", obs_module_text("Source.FitCanvas"));
	obs_property_set_long_description(fit, obs_module_text("Source.FitCanvas.Help"));
	obs_property_t *tickerWidthProp =
		obs_properties_add_int(design, "ticker_width", obs_module_text("Source.TickerWidth"), 0, 8192, 1);
	obs_property_int_set_suffix(tickerWidthProp, " px");
	obs_property_set_long_description(tickerWidthProp, obs_module_text("Source.TickerWidth.Help"));
	obs_properties_add_int(design, "width", obs_module_text("Source.Width"), 100, 4096, 1);
	obs_properties_add_int(design, "height", obs_module_text("Source.Height"), 100, 4096, 1);
	obs_property_t *align = obs_properties_add_list(design, "align", obs_module_text("Source.Align"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(align, obs_module_text("Source.Align.Bottom"), "bottom");
	obs_property_list_add_string(align, obs_module_text("Source.Align.Top"), "top");
	obs_property_t *scroll = obs_properties_add_list(design, "scroll", obs_module_text("Source.Scroll"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(scroll, obs_module_text("Source.Scroll.Continuous"), "continuous");
	obs_property_list_add_string(scroll, obs_module_text("Source.Scroll.Message"), "message");
	obs_property_t *speed =
		obs_properties_add_int_slider(design, "speed", obs_module_text("Source.Speed"), 20, 600, 10);
	obs_property_int_set_suffix(speed, " px/s");

	obs_properties_t *clock = addGroup(props, "group_clock", "Group.Clock");
	obs_property_t *clockOn = obs_properties_add_bool(clock, "clock", obs_module_text("Source.Clock"));
	obs_property_set_long_description(clockOn, obs_module_text("Source.Clock.Help"));
	obs_property_t *clockPosition = obs_properties_add_list(clock, "clock_position", obs_module_text("Clock.Position"),
								OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(clockPosition, obs_module_text("Clock.Position.Top"), "top");
	obs_property_list_add_string(clockPosition, obs_module_text("Clock.Position.Bottom"), "bottom");
	obs_properties_add_bool(clock, "clock_date", obs_module_text("Clock.Date"));
	obs_properties_add_bool(clock, "clock_time", obs_module_text("Clock.Time"));
	obs_properties_add_bool(clock, "clock_seconds", obs_module_text("Clock.Seconds"));
	obs_properties_add_bool(clock, "clock_hour24", obs_module_text("Clock.Hour24"));
	obs_properties_add_bool(clock, "clock_bar", obs_module_text("Clock.Bar"));

	// More options: platforms, icons and badges, disappearing, limits, spacing, margins, line
	// height and custom CSS.
	addAdvancedSwitch(props);
	obs_properties_t *advanced = addGroup(props, "group_advanced", "Group.Advanced");
	addPlatforms(advanced);
	obs_properties_add_bool(advanced, "icons", obs_module_text("Source.Icons"));
	obs_properties_add_bool(advanced, "badges", obs_module_text("Source.Badges"));
	obs_property_t *fade = obs_properties_add_int(advanced, "fade", obs_module_text("Source.Fade"), 0, 600, 1);
	obs_property_int_set_suffix(fade, " s");
	obs_property_set_long_description(fade, obs_module_text("Source.Fade.Help"));
	obs_properties_add_int(advanced, "max", obs_module_text("Source.Max"), 1, 100, 1);
	obs_property_t *spacing =
		obs_properties_add_int_slider(advanced, "spacing", obs_module_text("Source.Spacing"), -40, 60, 1);
	obs_property_int_set_suffix(spacing, " px");
	obs_property_t *hspacing =
		obs_properties_add_int_slider(advanced, "hspacing", obs_module_text("Source.Spacing"), -200, 400, 2);
	obs_property_int_set_suffix(hspacing, " px");
	addPadding(advanced, "pad_y", "pad_x", "Source.PadY", "Source.PadX");
	addLineHeight(advanced, Kind::Chat);
	addCustomCss(advanced);
	hookAll(props, Kind::Chat);
	return props;
}

// The Style group of the box sources (featured, viewers, clock): preset, font size and text.
void addStyleGroup(obs_properties_t *props, Kind kind, int maxFontSize)
{
	obs_properties_t *style = addGroup(props, "group_style", "Group.Style");
	addPreset(style);
	const int minFontSize = kind == Kind::Featured ? 12 : 10;
	obs_property_t *size = obs_properties_add_int_slider(style, "font_size", obs_module_text("Source.FontSize"),
							     minFontSize, maxFontSize, 1);
	obs_property_int_set_suffix(size, " px");
	addTextStyle(style, kind);
}

// The Background group of the box sources: its checkbox, the style list and the box controls.
void addBoxBackgroundGroup(obs_properties_t *props)
{
	obs_properties_t *background = addGroup(props, "group_background", "Group.Background");
	obs_properties_add_bool(background, "background", obs_module_text("Viewers.Background"));
	addStyleList(background);
	addBoxStyle(background, "");
}

obs_properties_t *vw_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_button2(props, "chats", obs_module_text("Source.Chats"), ov_open_settings, nullptr);
	addStyleGroup(props, Kind::Viewers, 120);
	addBoxBackgroundGroup(props);

	obs_properties_t *design = addGroup(props, "group_layout", "Group.Layout");
	obs_property_t *layout = obs_properties_add_list(design, "layout", obs_module_text("Viewers.Layout"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(layout, obs_module_text("Viewers.Layout.Row"), "row");
	obs_property_list_add_string(layout, obs_module_text("Viewers.Layout.Column"), "column");
	obs_properties_add_bool(design, "total", obs_module_text("Viewers.Total"));
	obs_properties_add_bool(design, "names", obs_module_text("Viewers.Names"));

	addAdvancedSwitch(props);
	obs_properties_t *advanced = addGroup(props, "group_advanced", "Group.Advanced");
	addPlatforms(advanced);
	addPadding(advanced, "pad_y", "pad_x", "Box.PadY", "Box.PadX");
	addCustomCss(advanced);
	hookAll(props, Kind::Viewers);
	return props;
}

// The legacy date and time source follows the same order: style, background, date and time.
obs_properties_t *ck_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	addStyleGroup(props, Kind::Clock, 160);
	addBoxBackgroundGroup(props);

	obs_properties_t *clock = addGroup(props, "group_clock", "Group.Clock");
	obs_properties_add_bool(clock, "date", obs_module_text("Clock.Date"));
	obs_properties_add_bool(clock, "time", obs_module_text("Clock.Time"));
	obs_properties_add_bool(clock, "seconds", obs_module_text("Clock.Seconds"));
	obs_properties_add_bool(clock, "hour24", obs_module_text("Clock.Hour24"));
	obs_properties_add_bool(clock, "bar", obs_module_text("Clock.Bar"));

	addAdvancedSwitch(props);
	obs_properties_t *advanced = addGroup(props, "group_advanced", "Group.Advanced");
	addPadding(advanced, "pad_y", "pad_x", "Box.PadY", "Box.PadX");
	addCustomCss(advanced);
	hookAll(props, Kind::Clock);
	return props;
}

// Los tiempos de "ocultar solo" se ven con la casilla activada; el fondo, con la suya.
bool fe_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	const bool autoHide = obs_data_get_bool(settings, "auto_hide");
	for (const char *key : {"hide_base", "hide_per_char", "hide_max"})
		obs_property_set_visible(obs_properties_get(props, key), autoHide);
	return box_changed(props, property, settings);
}

obs_properties_t *fe_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	addStyleGroup(props, Kind::Featured, 120);
	addBoxBackgroundGroup(props);

	obs_properties_t *design = addGroup(props, "group_layout", "Group.Layout");
	obs_property_t *valign = obs_properties_add_list(design, "valign", obs_module_text("Featured.VAlign"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const char *id : {"top", "center", "bottom"})
		obs_property_list_add_string(valign, obs_module_text(("Featured.VAlign." + std::string(id)).c_str()), id);
	obs_property_t *halign = obs_properties_add_list(design, "halign", obs_module_text("Featured.HAlign"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const char *id : {"left", "center", "right"})
		obs_property_list_add_string(halign, obs_module_text(("Featured.HAlign." + std::string(id)).c_str()), id);
	obs_properties_add_int(design, "width", obs_module_text("Source.Width"), 100, 4096, 1);
	obs_properties_add_int(design, "height", obs_module_text("Source.Height"), 100, 4096, 1);

	// More options: icons and badges, animation and auto-hide, padding, line height and CSS.
	addAdvancedSwitch(props);
	obs_properties_t *advanced = addGroup(props, "group_advanced", "Group.Advanced");
	obs_properties_add_bool(advanced, "icons", obs_module_text("Source.Icons"));
	obs_properties_add_bool(advanced, "badges", obs_module_text("Source.Badges"));
	obs_property_t *anim = obs_properties_add_list(advanced, "animation", obs_module_text("Featured.Animation"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const char *id : {"slide", "fade", "zoom", "none"})
		obs_property_list_add_string(anim, obs_module_text(("Featured.Animation." + std::string(id)).c_str()), id);
	obs_property_t *autoHide = obs_properties_add_bool(advanced, "auto_hide", obs_module_text("Featured.AutoHide"));
	obs_property_set_long_description(autoHide, obs_module_text("Featured.AutoHide.Help"));
	obs_property_t *base =
		obs_properties_add_int(advanced, "hide_base", obs_module_text("Featured.HideBase"), 1, 300, 1);
	obs_property_int_set_suffix(base, " s");
	obs_property_t *perChar =
		obs_properties_add_int(advanced, "hide_per_char", obs_module_text("Featured.HidePerChar"), 0, 1000, 5);
	obs_property_int_set_suffix(perChar, " ms");
	obs_property_t *max = obs_properties_add_int(advanced, "hide_max", obs_module_text("Featured.HideMax"), 1, 600, 1);
	obs_property_int_set_suffix(max, " s");
	addPadding(advanced, "pad_y", "pad_x", "Box.PadY", "Box.PadX");
	addLineHeight(advanced, Kind::Featured);
	addCustomCss(advanced);
	hookAll(props, Kind::Featured);
	return props;
}

} // namespace overlay_source
