// Sizes of the viewers and clock boxes (see box-measure.hpp).

#include "box-measure.hpp"

#include <QDate>
#include <QDateTime>
#include <QFont>
#include <QFontMetricsF>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace overlay_source {

// Mismo formato que viewers.html: 1.234 · 12.5K · 1.2M
QString formatViewers(int n)
{
	if (n < 0)
		return QStringLiteral("—");
	if (n < 10000)
		return QLocale().toString(n);
	if (n < 1000000)
		return QString::number(n / 1000.0, 'f', n < 100000 ? 1 : 0).remove(QRegularExpression("\\.0$")) + 'K';
	return QString::number(n / 1000000.0, 'f', 1).remove(QRegularExpression("\\.0$")) + 'M';
}

// Font used to measure a box: the chosen family (if any) before the default stack, with the
// chosen weight. The page then scales the box, so an approximation is enough.
QFont measureFont(const QJsonObject &config, double em, int defaultWeight)
{
	QFont font;
	QStringList families{"Segoe UI", ".AppleSystemUIFont", "Helvetica Neue", "Roboto"};
	const QString face = config.value("fontFace").toString().trimmed();
	if (!face.isEmpty())
		families.prepend(face);
	font.setFamilies(families);
	font.setPixelSize(std::max(1, int(em)));
	font.setWeight(QFont::Weight(std::clamp(config.value("textWeight").toInt(defaultWeight), 100, 900)));
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	font.setFeature(QFont::Tag("tnum"), 1); // cifras de ancho fijo, como font-variant-numeric
#endif
	return font;
}

// Lo que suma el borde del recuadro a cada dimensión (los estilos con borde propio usan hasta 2 px).
double boxBorder(const QJsonObject &config)
{
	if (!config.value("background").toBool())
		return 0;
	return 2.0 * std::max(2, config.value("bgBorderWidth").toInt());
}

// Tamaño del recuadro con las medidas de viewers.html (en em = tamaño de letra). La página
// luego escala el recuadro para que llene la fuente justo, así que basta con aproximarlo.
QSize viewerBoxSize(const QJsonObject &config, const QList<ViewerItem> &items, int total)
{
	const double em = std::max(1, config.value("size").toInt(28));
	const QFont font = measureFont(config, em, 700);
	QFont small = font;
	small.setPixelSize(std::max(1, int(em * 0.6)));
	small.setWeight(QFont::DemiBold);
	const QFontMetricsF big(font), little(small);
	const bool names = config.value("names").toBool();
	const bool column = config.value("layout").toString() == "column";
	const double padX = 2.0 * config.value("padX").toInt(17), padY = 2.0 * config.value("padY").toInt(10);

	QList<double> widths;
	for (const ViewerItem &item : items)
		widths << em * 1.45 + big.horizontalAdvance(formatViewers(item.viewers)) +
				  (names ? em * 0.35 + little.horizontalAdvance(item.name) : 0);
	const double totalWidth = total >= 0 ? em * 1.45 + big.horizontalAdvance(formatViewers(total)) : 0;

	double w, h;
	if (column) {
		w = totalWidth;
		for (double x : widths)
			w = std::max(w, x);
		w += padX;
		h = padY + double(items.size()) * em * 1.2 + double(items.size() - 1) * em * 0.3;
		if (total >= 0)
			h += em * 1.75 + 2;
	} else {
		w = padX + em * 0.9 * double(items.size() - 1);
		for (double x : widths)
			w += x;
		if (total >= 0)
			w += em * 1.8 + 2 + totalWidth;
		h = em * 1.2 + padY;
	}
	const double border = boxBorder(config);
	return QSize(std::min(4096, int(std::ceil(w + border))), std::min(4096, int(std::ceil(h + border))));
}


// Tamaño del recuadro de clock.html con un texto de ejemplo del ancho máximo (la página luego
// escala el recuadro para llenar la fuente justo).
QSize clockBoxSize(const QJsonObject &c)
{
	const double em = std::max(1, c.value("size").toInt(32));
	const QFont font = measureFont(c, em, 800);
	const QDateTime sample(QDate(2026, 12, 28), QTime(23, 58, 58));
	QStringList parts;
	if (c.value("date").toBool())
		parts << QLocale().toString(sample.date(), "dd MMM yyyy").remove('.').toUpper();
	if (c.value("time").toBool())
		parts << QLocale()
				 .toString(sample.time(), QString(c.value("hour24").toBool() ? "HH:mm" : "hh:mm") +
								  (c.value("seconds").toBool() ? ":ss" : "") +
								  (c.value("hour24").toBool() ? "" : " AP"))
				 .toUpper();
	const QString text = parts.join(' ');
	double w = 2.0 * c.value("padX").toInt(19) + QFontMetricsF(font).horizontalAdvance(text) + em * 0.02 * double(text.size());
	if (c.value("bar").toBool())
		w += em * 0.63;
	const double border = boxBorder(c);
	return QSize(std::min(4096, int(std::ceil(w + border))), int(std::ceil(em * 1.2 + 2.0 * c.value("padY").toInt(11) + border)));
}

} // namespace overlay_source
