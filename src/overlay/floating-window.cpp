#include "floating-window.hpp"
#include "source-internal.hpp"

#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#include <QAction>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QImage>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <cstring>
#include <functional>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#elif defined(__APPLE__)
void floating_exclude_from_capture(void *nsView);
#endif

namespace floating {

using overlay_source::OverlaySource;

namespace {

constexpr uint64_t kFrameNs = 33'000'000; // unas 30 imágenes por segundo
constexpr int kEdge = 8;                  // px del borde que cambian el tamaño

class FloatingWindow;

} // namespace

// Lo comparten la fuente, el hilo de gráficos (pinta en tex y copia a frame) y la ventana.
struct State {
	obs_source_t *browser = nullptr; // la que se pinta: la de la fuente o la propia
	obs_source_t *own = nullptr;     // propia, con disposición distinta a la de la fuente
	QString layout;                  // "same", "vertical" u "horizontal"
	bool showing = false;

	std::mutex frameMutex;
	QImage frame;
	quint64 serial = 0;

	// Solo el hilo de gráficos.
	gs_texrender_t *tex = nullptr;
	gs_stagesurf_t *stage[2] = {};
	uint32_t stageW[2] = {}, stageH[2] = {};
	bool staged[2] = {};
	int next = 0;
	uint64_t lastNs = 0;

	QPointer<QWidget> window; // solo el hilo de la interfaz
};

namespace {

// Hilo de gráficos: copia la imagen pintada la vez anterior (ya está lista, no espera a la GPU)
// y pinta la fuente de nuevo.
void renderFrame(void *param, uint32_t, uint32_t)
{
	auto *st = static_cast<State *>(param);
	const uint64_t now = os_gettime_ns();
	if (!st->browser || now - st->lastNs < kFrameNs)
		return;
	st->lastNs = now;

	const int ready = st->next ^ 1;
	if (st->staged[ready]) {
		st->staged[ready] = false;
		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (gs_stagesurface_map(st->stage[ready], &data, &linesize)) {
			const uint32_t w = st->stageW[ready], h = st->stageH[ready];
			QImage image(int(w), int(h), QImage::Format_RGBA8888_Premultiplied);
			for (uint32_t y = 0; y < h; ++y)
				std::memcpy(image.scanLine(int(y)), data + size_t(y) * linesize, size_t(w) * 4);
			gs_stagesurface_unmap(st->stage[ready]);
			std::lock_guard<std::mutex> lock(st->frameMutex);
			st->frame = std::move(image);
			++st->serial;
		}
	}

	const uint32_t w = obs_source_get_width(st->browser);
	const uint32_t h = obs_source_get_height(st->browser);
	if (!w || !h)
		return;
	if (!st->tex)
		st->tex = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_texrender_reset(st->tex);
	if (!gs_texrender_begin(st->tex, w, h))
		return;
	vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, float(w), 0.0f, float(h), -100.0f, 100.0f);
	// La fuente de navegador da alfa premultiplicado: sobre transparente queda tal cual.
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	obs_source_video_render(st->browser);
	gs_blend_state_pop();
	gs_texrender_end(st->tex);

	const int i = st->next;
	if (!st->stage[i] || st->stageW[i] != w || st->stageH[i] != h) {
		gs_stagesurface_destroy(st->stage[i]);
		st->stage[i] = gs_stagesurface_create(w, h, GS_RGBA);
		st->stageW[i] = w;
		st->stageH[i] = h;
	}
	if (!st->stage[i])
		return;
	gs_stage_texture(st->stage[i], gs_texrender_get_texture(st->tex));
	st->staged[i] = true;
	st->next ^= 1;
}

// Alto de la página propia y cuánto agrandar la letra para una ventana de este tamaño. En vertical
// la página ocupa toda la ventana (más ancha, líneas más largas; más alta, más mensajes); en
// horizontal es una sola línea, así que con la ventana más alta crece la letra hasta llenarla.
int ownPageHeight(OverlaySource *s, const QString &layout, const QSize &window, double &scale)
{
	scale = 1.0;
	const QJsonObject base = overlay_source::floatConfig(s, layout, 1.0);
	if (base.value("layout").toString() != QLatin1String("horizontal"))
		return std::max(40, window.height());
	// El relleno no crece con la letra: se ajusta unas veces hasta que la línea llena el alto.
	const double target = window.height();
	scale = std::clamp(target / overlay_source::tickerHeight(base), 0.5, 8.0);
	for (int i = 0; i < 3; ++i)
		scale = std::clamp(scale * target / overlay_source::tickerHeight(overlay_source::floatConfig(s, layout, scale)),
				   0.5, 8.0);
	const int scaled = int(overlay_source::tickerHeight(overlay_source::floatConfig(s, layout, scale)));
	return std::max(10, std::min(window.height(), scaled));
}

// La página propia tiene el tamaño de la ventana. Hilo de la interfaz.
void syncOwnBrowser(obs_weak_source_t *weak, State &st, const QSize &window)
{
	if (!st.own)
		return;
	obs_source_t *source = obs_weak_source_get_source(weak);
	if (!source)
		return;
	if (auto *s = static_cast<OverlaySource *>(obs_obj_get_data(source))) {
		double scale = 1.0;
		const int h = ownPageHeight(s, st.layout, window, scale);
		{
			std::lock_guard<std::mutex> lock(s->floatingMutex);
			s->floatScale = scale;
		}
		obs_data_t *bs = overlay_source::browserSettings(s);
		obs_data_set_int(bs, "width", std::max(80, window.width()));
		obs_data_set_int(bs, "height", h);
		obs_source_update(st.own, bs);
		obs_data_release(bs);
		overlay_source::sendFloatConfig(s);
	}
	obs_source_release(source);
}

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// Cambia un ajuste de la fuente como si se hiciera en sus propiedades (vuelve por apply()).
void setSetting(obs_weak_source_t *weak, const std::function<void(obs_data_t *)> &change)
{
	obs_source_t *src = obs_weak_source_get_source(weak);
	if (!src)
		return;
	obs_data_t *data = obs_data_create();
	change(data);
	obs_source_update(src, data);
	obs_data_release(data);
	obs_source_release(src);
}

class FloatingWindow : public QWidget {
public:
	FloatingWindow(std::shared_ptr<State> state, obs_source_t *source, obs_data_t *settings)
		: QWidget(nullptr,
			  Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint),
		  state_(std::move(state)),
		  weak_(obs_source_get_weak_source(source))
	{
		setAttribute(Qt::WA_TranslucentBackground);
		setAttribute(Qt::WA_ShowWithoutActivating);
		setAttribute(Qt::WA_MacAlwaysShowToolWindow);
		setAttribute(Qt::WA_DeleteOnClose, false);
		setWindowTitle(QString::fromUtf8(obs_source_get_name(source)));
		setMouseTracking(true);
		setMinimumSize(80, 40);

		// Donde quedó la última vez; la primera, abajo a la derecha de la pantalla principal.
		const int w = int(obs_data_get_int(settings, "float_w"));
		const int h = int(obs_data_get_int(settings, "float_h"));
		if (w > 0 && h > 0) {
			setGeometry(int(obs_data_get_int(settings, "float_x")), int(obs_data_get_int(settings, "float_y")),
				    w, h);
		} else {
			const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
			QSize size(int(std::max(1u, obs_source_get_width(source))),
				   int(std::max(1u, obs_source_get_height(source))));
			size.scale(screen.width() / 3, screen.height() * 2 / 3, Qt::KeepAspectRatio);
			auto *s = static_cast<OverlaySource *>(obs_obj_get_data(source));
			const QString layout = s && state_->own ? overlay_source::floatConfig(s, state_->layout, 1.0)
									  .value("layout")
									  .toString()
								: QString();
			if (layout == QLatin1String("horizontal"))
				size = QSize(screen.width() / 2, int(overlay_source::tickerHeight(
									 overlay_source::floatConfig(s, state_->layout, 1.0))));
			else if (layout == QLatin1String("vertical"))
				size = QSize(420, std::min(600, screen.height() * 2 / 3));
			resize(size);
			move(screen.right() - size.width() - 24, screen.bottom() - size.height() - 24);
		}

		poll_.setInterval(33);
		connect(&poll_, &QTimer::timeout, this, [this] {
			QImage image;
			{
				std::lock_guard<std::mutex> lock(state_->frameMutex);
				if (state_->serial == shown_)
					return;
				shown_ = state_->serial;
				image = state_->frame;
			}
			frame_ = std::move(image);
			update();
		});
		poll_.start();
		save_.setSingleShot(true);
		save_.setInterval(500);
		connect(&save_, &QTimer::timeout, this, [this] { saveGeometry(); });
		fit_.setSingleShot(true);
		fit_.setInterval(120);
		connect(&fit_, &QTimer::timeout, this, [this] { syncOwnBrowser(weak_, *state_, size()); });
		// Recién abierta se ve su borde unos segundos, para encontrarla aunque no haya mensajes.
		outlineUntil_ = os_gettime_ns() + 4'000'000'000ULL;
		QTimer::singleShot(4100, this, [this] { update(); });
	}

	~FloatingWindow() override { obs_weak_source_release(weak_); }

	void setOptions(bool clickThrough, int opacity)
	{
		setWindowOpacity(std::clamp(opacity, 10, 100) / 100.0);
		if (clickThrough == clickThrough_ && isVisible())
			return;
		clickThrough_ = clickThrough;
		// Cambiar las banderas oculta la ventana: se vuelve a mostrar.
		setWindowFlag(Qt::WindowTransparentForInput, clickThrough);
		show();
		update();
	}

protected:
	// La ventana es solo para quien transmite: se saca de las capturas de pantalla, para que una
	// fuente de captura de pantalla de OBS no la muestre. Va en cada show: cambiar las banderas
	// crea la ventana nativa de nuevo. Windows 10 2004+ y macOS; en Linux no hay forma (ni X11 ni
	// Wayland dejan excluir una ventana): ahí se captura la ventana del juego, no la pantalla.
	void showEvent(QShowEvent *event) override
	{
		QWidget::showEvent(event);
#ifdef _WIN32
		SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), WDA_EXCLUDEFROMCAPTURE);
#elif defined(__APPLE__)
		floating_exclude_from_capture(reinterpret_cast<void *>(winId()));
#endif
	}

	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		// Casi transparente pero no del todo: en Windows los píxeles transparentes no reciben el
		// ratón, y la ventana debe poder moverse desde cualquier punto.
		p.fillRect(rect(), clickThrough_ ? QColor(0, 0, 0, 0) : QColor(0, 0, 0, 1));
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		if (!frame_.isNull()) {
			QSize size = frame_.size();
			size.scale(this->size(), Qt::KeepAspectRatio);
			const QRect target((width() - size.width()) / 2, height() - size.height(), size.width(),
					   size.height());
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			p.drawImage(target, frame_);
		}
		if (!clickThrough_ && (hover_ || os_gettime_ns() < outlineUntil_)) {
			p.setRenderHint(QPainter::Antialiasing);
			QPen pen(QColor(255, 255, 255, 170), 1.5, Qt::DashLine);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 6, 6);
			p.setPen(QPen(QColor(255, 255, 255, 200), 2));
			const QPoint c = rect().bottomRight();
			for (int d = 6; d <= 14; d += 4)
				p.drawLine(c.x() - d, c.y() - 3, c.x() - 3, c.y() - d);
		}
	}

	void enterEvent(QEnterEvent *) override
	{
		hover_ = true;
		update();
	}

	void leaveEvent(QEvent *) override
	{
		hover_ = false;
		unsetCursor();
		update();
	}

	void mouseMoveEvent(QMouseEvent *event) override
	{
		const Qt::Edges e = edgesAt(event->position().toPoint());
		if ((e & Qt::LeftEdge && e & Qt::TopEdge) || (e & Qt::RightEdge && e & Qt::BottomEdge))
			setCursor(Qt::SizeFDiagCursor);
		else if ((e & Qt::RightEdge && e & Qt::TopEdge) || (e & Qt::LeftEdge && e & Qt::BottomEdge))
			setCursor(Qt::SizeBDiagCursor);
		else if (e & (Qt::LeftEdge | Qt::RightEdge))
			setCursor(Qt::SizeHorCursor);
		else if (e & (Qt::TopEdge | Qt::BottomEdge))
			setCursor(Qt::SizeVerCursor);
		else
			setCursor(Qt::SizeAllCursor);
	}

	// Mover y cambiar el tamaño los hace el sistema: así funciona igual en Windows, macOS y Linux.
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() != Qt::LeftButton || !windowHandle())
			return;
		const Qt::Edges e = edgesAt(event->position().toPoint());
		if (e)
			windowHandle()->startSystemResize(e);
		else
			windowHandle()->startSystemMove();
	}

	void contextMenuEvent(QContextMenuEvent *event) override
	{
		QMenu menu(this);
		QAction *through = menu.addAction(text("Float.ClickThrough"));
		through->setToolTip(text("Float.ClickThrough.Help"));
		QMenu *opacity = menu.addMenu(text("Float.Opacity"));
		const int current = qRound(windowOpacity() * 100);
		for (int value : {100, 85, 70, 55, 40}) {
			QAction *a = opacity->addAction(QStringLiteral("%1 %").arg(value));
			a->setCheckable(true);
			a->setChecked(current == value);
			connect(a, &QAction::triggered, this, [this, value] {
				setSetting(weak_, [value](obs_data_t *d) { obs_data_set_int(d, "float_opacity", value); });
			});
		}
		menu.addSeparator();
		QAction *closeAction = menu.addAction(text("Float.Close"));
		connect(through, &QAction::triggered, this, [this] {
			setSetting(weak_, [](obs_data_t *d) { obs_data_set_bool(d, "float_clickthrough", true); });
		});
		connect(closeAction, &QAction::triggered, this, [this] {
			setSetting(weak_, [](obs_data_t *d) { obs_data_set_bool(d, "float_window", false); });
		});
		menu.exec(event->globalPos());
	}

	void moveEvent(QMoveEvent *event) override
	{
		QWidget::moveEvent(event);
		save_.start();
	}

	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event);
		save_.start();
		fit_.start();
	}

public:
	// Se guarda en los ajustes de la fuente (sin actualizarla): vuelve a abrirse en el mismo lugar.
	void saveGeometry()
	{
		obs_source_t *src = obs_weak_source_get_source(weak_);
		if (!src)
			return;
		obs_data_t *settings = obs_source_get_settings(src);
		const QRect g = geometry();
		obs_data_set_int(settings, "float_x", g.x());
		obs_data_set_int(settings, "float_y", g.y());
		obs_data_set_int(settings, "float_w", g.width());
		obs_data_set_int(settings, "float_h", g.height());
		obs_data_release(settings);
		obs_source_release(src);
	}

private:
	Qt::Edges edgesAt(const QPoint &pos) const
	{
		Qt::Edges e;
		if (pos.x() < kEdge)
			e |= Qt::LeftEdge;
		if (pos.x() >= width() - kEdge)
			e |= Qt::RightEdge;
		if (pos.y() < kEdge)
			e |= Qt::TopEdge;
		if (pos.y() >= height() - kEdge)
			e |= Qt::BottomEdge;
		return e;
	}

	std::shared_ptr<State> state_;
	obs_weak_source_t *weak_;
	QImage frame_;
	quint64 shown_ = 0;
	QTimer poll_;
	QTimer save_;
	QTimer fit_;
	bool clickThrough_ = false;
	bool hover_ = false;
	uint64_t outlineUntil_ = 0;
};

struct Options {
	bool enabled = false;
	bool clickThrough = false;
	int opacity = 100;
	QString layout = QStringLiteral("same");
	QString clock = QStringLiteral("same");
	QString style = QStringLiteral("same");
};

Options readOptions(obs_data_t *settings)
{
	Options o;
	o.enabled = obs_data_get_bool(settings, "float_window");
	o.clickThrough = obs_data_get_bool(settings, "float_clickthrough");
	o.opacity = int(obs_data_get_int(settings, "float_opacity"));
	const QString layout = QString::fromUtf8(obs_data_get_string(settings, "float_layout"));
	if (layout == QLatin1String("vertical") || layout == QLatin1String("horizontal"))
		o.layout = layout;
	const QString clock = QString::fromUtf8(obs_data_get_string(settings, "float_clock"));
	if (clock == QLatin1String("show") || clock == QLatin1String("hide"))
		o.clock = clock;
	if (strcmp(obs_data_get_string(settings, "float_style"), "clean") == 0)
		o.style = QStringLiteral("clean");
	return o;
}

// Hilo de la interfaz, con una referencia a la fuente.
void applyOnUi(OverlaySource *s, obs_source_t *source, const Options &o)
{
	std::shared_ptr<State> st;
	{
		std::lock_guard<std::mutex> lock(s->floatingMutex);
		st = s->floating;
	}
	if (!o.enabled) {
		close(s);
		return;
	}
	if (!s->browser)
		return;
	// Solo el chat tiene disposición; cambiarla abre la ventana de nuevo con otra página.
	// "disposición|fecha y hora|estilo". El chat siempre tiene su propia página, del tamaño de la
	// ventana: al estirarla cabe más chat en vez de agrandarse la imagen. Las demás fuentes copian
	// la imagen de la de la escena.
	const QString kShared = QStringLiteral("same|same|same");
	const QString layout = s->kind == overlay_source::Kind::Chat
				       ? o.layout + QLatin1Char('|') + o.clock + QLatin1Char('|') + o.style
				       : kShared;
	if (st && st->layout != layout) {
		if (auto *window = static_cast<FloatingWindow *>(st->window.data()))
			window->saveGeometry();
		close(s);
		st.reset();
	}
	if (!st) {
		st = std::make_shared<State>();
		st->layout = layout;
		st->browser = s->browser;
		if (s->kind == overlay_source::Kind::Chat) {
			obs_data_t *bs = overlay_source::browserSettings(s);
			st->own = obs_source_create_private("browser_source", "multichat-float", bs);
			obs_data_release(bs);
			if (st->own)
				st->browser = st->own;
		}
		// La fuente de navegador se pausa si nadie la muestra: la ventana cuenta como mostrarla.
		obs_source_inc_showing(st->browser);
		st->showing = true;
		obs_data_t *settings = obs_source_get_settings(source);
		st->window = new FloatingWindow(st, source, settings);
		obs_data_release(settings);
		{
			std::lock_guard<std::mutex> lock(s->floatingMutex);
			s->floating = st;
			s->floatBrowser = st->own;
			s->floatLayout = layout;
		}
		obs_add_main_render_callback(renderFrame, st.get());
	}
	// La página propia sigue el tamaño de la ventana y los ajustes de la fuente (el alto de la
	// cinta depende del tamaño de letra).
	if (st->own && st->window) {
		obs_weak_source_t *weak = obs_source_get_weak_source(source);
		syncOwnBrowser(weak, *st, st->window->size());
		obs_weak_source_release(weak);
	}
	if (auto *window = static_cast<FloatingWindow *>(st->window.data()))
		window->setOptions(o.clickThrough, o.opacity);
}

} // namespace

void addProperties(obs_properties_t *props, bool chat)
{
	obs_properties_t *group = obs_properties_create();
	obs_properties_add_group(props, "group_float", obs_module_text("Group.Float"), OBS_GROUP_NORMAL, group);
	obs_property_t *on = obs_properties_add_bool(group, "float_window", obs_module_text("Float.Show"));
	obs_property_set_long_description(on, obs_module_text("Float.Show.Help"));
	if (chat) {
		obs_property_t *layout = obs_properties_add_list(group, "float_layout", obs_module_text("Float.Layout"),
								 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(layout, obs_module_text("Float.Layout.Same"), "same");
		obs_property_list_add_string(layout, obs_module_text("Source.Layout.Vertical"), "vertical");
		obs_property_list_add_string(layout, obs_module_text("Source.Layout.Horizontal"), "horizontal");
		obs_property_set_long_description(layout, obs_module_text("Float.Layout.Help"));
		obs_property_t *clock = obs_properties_add_list(group, "float_clock", obs_module_text("Float.Clock"),
								OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(clock, obs_module_text("Float.Clock.Same"), "same");
		obs_property_list_add_string(clock, obs_module_text("Float.Clock.Show"), "show");
		obs_property_list_add_string(clock, obs_module_text("Float.Clock.Hide"), "hide");
		obs_property_set_long_description(clock, obs_module_text("Float.Clock.Help"));
		obs_property_t *style = obs_properties_add_list(group, "float_style", obs_module_text("Float.Style"),
								OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(style, obs_module_text("Float.Style.Same"), "same");
		obs_property_list_add_string(style, obs_module_text("Float.Style.Clean"), "clean");
		obs_property_set_long_description(style, obs_module_text("Float.Style.Help"));
	}
	obs_property_t *only = obs_properties_add_bool(group, "float_only", obs_module_text("Float.Only"));
	obs_property_set_long_description(only, obs_module_text("Float.Only.Help"));
	obs_property_t *through =
		obs_properties_add_bool(group, "float_clickthrough", obs_module_text("Float.ClickThrough"));
	obs_property_set_long_description(through, obs_module_text("Float.ClickThrough.Help"));
	obs_property_t *opacity =
		obs_properties_add_int_slider(group, "float_opacity", obs_module_text("Float.Opacity"), 10, 100, 5);
	obs_property_int_set_suffix(opacity, " %");
}

// Las opciones de la ventana solo se ven con la ventana activada.
void showProperties(obs_properties_t *props, obs_data_t *settings)
{
	const bool on = obs_data_get_bool(settings, "float_window");
	for (const char *key : {"float_layout", "float_clock", "float_style", "float_only", "float_clickthrough", "float_opacity"})
		if (obs_property_t *p = obs_properties_get(props, key))
			obs_property_set_visible(p, on);
}

void defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "float_window", false);
	obs_data_set_default_bool(settings, "float_only", false);
	obs_data_set_default_string(settings, "float_layout", "same");
	obs_data_set_default_string(settings, "float_clock", "same");
	obs_data_set_default_string(settings, "float_style", "same");
	obs_data_set_default_bool(settings, "float_clickthrough", false);
	obs_data_set_default_int(settings, "float_opacity", 100);
}

void apply(OverlaySource *s, obs_data_t *settings)
{
	const Options o = readOptions(settings);
	// Sin la ventana abierta no tendría sentido esconderla: se vería en ningún sitio.
	s->floatOnly = o.enabled && obs_data_get_bool(settings, "float_only");
	{
		std::lock_guard<std::mutex> lock(s->floatingMutex);
		if (!o.enabled && !s->floating)
			return; // nada que abrir ni cerrar
	}
	obs_weak_source_t *weak = obs_source_get_weak_source(s->self);
	QMetaObject::invokeMethod(
		qApp,
		[weak, o] {
			obs_source_t *source = obs_weak_source_get_source(weak);
			obs_weak_source_release(weak);
			if (!source)
				return;
			if (auto *s = static_cast<OverlaySource *>(obs_obj_get_data(source)))
				applyOnUi(s, source, o);
			obs_source_release(source);
		},
		Qt::QueuedConnection);
}

void close(OverlaySource *s)
{
	std::shared_ptr<State> st;
	{
		std::lock_guard<std::mutex> lock(s->floatingMutex);
		st.swap(s->floating);
		s->floatBrowser = nullptr; // desde aquí nadie más le manda mensajes
	}
	if (!st)
		return;
	// Al volver, el hilo de gráficos ya no está pintando para esta ventana.
	obs_remove_main_render_callback(renderFrame, st.get());
	if (st->showing && st->browser)
		obs_source_dec_showing(st->browser);
	st->browser = nullptr;
	obs_source_release(st->own);
	st->own = nullptr;
	obs_enter_graphics();
	gs_texrender_destroy(st->tex);
	gs_stagesurface_destroy(st->stage[0]);
	gs_stagesurface_destroy(st->stage[1]);
	obs_leave_graphics();
	st->tex = nullptr;
	st->stage[0] = st->stage[1] = nullptr;
	QMetaObject::invokeMethod(
		qApp,
		[st] {
			if (st->window)
				st->window->deleteLater();
		},
		Qt::QueuedConnection);
}

void toggleClickThroughAll()
{
	overlay_source::forEachSource([](OverlaySource *s) {
		{
			std::lock_guard<std::mutex> lock(s->floatingMutex);
			if (!s->floating)
				return;
		}
		obs_data_t *settings = obs_source_get_settings(s->self);
		const bool through = obs_data_get_bool(settings, "float_clickthrough");
		obs_data_release(settings);
		obs_data_t *change = obs_data_create();
		obs_data_set_bool(change, "float_clickthrough", !through);
		obs_source_update(s->self, change);
		obs_data_release(change);
	});
}

} // namespace floating
