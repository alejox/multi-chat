#pragma once

// Sizes of the viewers and clock boxes, measured with the page's font. Only QtCore and QtGui:
// no OBS, so the tests can use it. QFont and QFontMetricsF are only safe on the UI thread
// (see requestMeasure in viewers-layout.cpp).

#include <QJsonObject>
#include <QList>
#include <QSize>
#include <QString>

class QFont;

namespace overlay_source {

struct ViewerItem {
	QString platform, name;
	int viewers = -1;
};

QString formatViewers(int n);
QFont measureFont(const QJsonObject &config, double em, int defaultWeight);
double boxBorder(const QJsonObject &config);
QSize viewerBoxSize(const QJsonObject &config, const QList<ViewerItem> &items, int total);
QSize clockBoxSize(const QJsonObject &c);

} // namespace overlay_source
