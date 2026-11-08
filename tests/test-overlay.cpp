// Tests for the pure overlay helpers (CSS values from OBS settings), without OBS.

#include "../src/overlay-presets.hpp"

#include <QtTest/QtTest>

#include <string>

using namespace overlay;

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
};

QTEST_GUILESS_MAIN(OverlayTest)
#include "test-overlay.moc"
