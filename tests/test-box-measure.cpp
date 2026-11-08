// Tests for the viewers and clock box sizes (src/overlay/box-measure.cpp), without OBS.
// QFont needs a GUI application: it runs on the offscreen platform, so no display is needed.

#include "../src/overlay/box-measure.hpp"

#include <QGuiApplication>
#include <QtTest/QtTest>

using namespace overlay_source;

namespace {

QList<ViewerItem> channels(int n)
{
	const QList<ViewerItem> all = {{"twitch", "micanal", 1234},
				       {"youtube", "@micanal", 856},
				       {"kick", "micanal", 312},
				       {"tiktok", "@micanal", 2045}};
	return all.mid(0, n);
}

QJsonObject clockConfig(bool date, bool time, bool seconds, bool bar)
{
	return QJsonObject{{"size", 32},   {"date", date}, {"time", time}, {"seconds", seconds},
			   {"hour24", true}, {"bar", bar},   {"padX", 19},   {"padY", 11}};
}

} // namespace

class BoxMeasureTest : public QObject {
	Q_OBJECT

private slots:
	void formatsLikeTheViewersPage()
	{
		QCOMPARE(formatViewers(-1), QStringLiteral("—"));
		QCOMPARE(formatViewers(12500), QStringLiteral("12.5K"));
		QCOMPARE(formatViewers(100000), QStringLiteral("100K"));
		QCOMPARE(formatViewers(1000000), QStringLiteral("1M"));
		QCOMPARE(formatViewers(1250000), QStringLiteral("1.3M"));
	}

	void borderOnlyWithBackground()
	{
		QCOMPARE(boxBorder(QJsonObject{{"background", false}, {"bgBorderWidth", 5}}), 0.0);
		QCOMPARE(boxBorder(QJsonObject{{"background", true}, {"bgBorderWidth", 0}}), 4.0);
		QCOMPARE(boxBorder(QJsonObject{{"background", true}, {"bgBorderWidth", 5}}), 10.0);
	}

	void moreChannelsMakeAWiderRow()
	{
		const QJsonObject config{{"size", 28}, {"layout", "row"}};
		const QSize one = viewerBoxSize(config, channels(1), -1);
		const QSize three = viewerBoxSize(config, channels(3), -1);
		QVERIFY(one.width() > 0 && one.height() > 0);
		QVERIFY(three.width() > one.width());
		QCOMPARE(three.height(), one.height());
	}

	void aColumnGrowsDown()
	{
		const QJsonObject row{{"size", 28}, {"layout", "row"}};
		const QJsonObject column{{"size", 28}, {"layout", "column"}};
		const QSize r = viewerBoxSize(row, channels(3), -1);
		const QSize c = viewerBoxSize(column, channels(3), -1);
		QVERIFY(c.height() > r.height());
		QVERIFY(c.width() < r.width());
	}

	void namesAndTotalAddRoom()
	{
		const QJsonObject plain{{"size", 28}};
		const QJsonObject named{{"size", 28}, {"names", true}};
		QVERIFY(viewerBoxSize(named, channels(2), -1).width() > viewerBoxSize(plain, channels(2), -1).width());
		QVERIFY(viewerBoxSize(plain, channels(2), 2090).width() > viewerBoxSize(plain, channels(2), -1).width());
	}

	void boxesAreCapped()
	{
		const QJsonObject huge{{"size", 4000}, {"layout", "row"}};
		const QSize size = viewerBoxSize(huge, channels(4), 4447);
		QVERIFY(size.width() <= 4096);
		QVERIFY(size.height() <= 4096);
		QVERIFY(clockBoxSize(QJsonObject{{"size", 4000}, {"date", true}, {"time", true}}).width() <= 4096);
	}

	void clockWidthFollowsWhatItShows()
	{
		const int timeOnly = clockBoxSize(clockConfig(false, true, false, false)).width();
		QVERIFY(clockBoxSize(clockConfig(false, true, true, false)).width() > timeOnly);
		QVERIFY(clockBoxSize(clockConfig(true, true, false, false)).width() > timeOnly);
		QVERIFY(clockBoxSize(clockConfig(false, true, false, true)).width() > timeOnly);
		// Height: one line of text plus the padding.
		QCOMPARE(clockBoxSize(clockConfig(false, true, false, false)).height(), int(std::ceil(32 * 1.2 + 22)));
	}
};

int main(int argc, char **argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		qputenv("QT_QPA_PLATFORM", "offscreen");
	QGuiApplication app(argc, argv);
	QLocale::setDefault(QLocale::c());
	BoxMeasureTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "test-box-measure.moc"
