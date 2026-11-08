#pragma once

// The supported platforms, in display order: the one table for ids, names and brand colors.
// data/overlay/icons.js keeps its own copy for the pages (tests/test-overlay.cpp checks it matches).

#include <QString>

#include <cstdint>

namespace platforms {

struct Platform {
	const char *id;
	const char *name;
	std::uint32_t color; // 0xRRGGBB
};

// Real platforms, in the order used by the dock, the settings and the on-screen sources.
inline constexpr Platform kAll[] = {
	{"twitch", "Twitch", 0x9146FF},  {"youtube", "YouTube", 0xFF0033},   {"kick", "Kick", 0x53FC18},
	{"tiktok", "TikTok", 0xFE2C55}, {"starvios", "Starvios", 0x8B5CF6},
};
// The test chat ("demo:demo"): after the real ones.
inline constexpr Platform kDemo = {"demo", "Demo", 0x888888};
// Color for anything unknown.
inline constexpr std::uint32_t kUnknownColor = 0x888888;

inline const Platform *find(const QString &id)
{
	for (const Platform &p : kAll)
		if (id == QLatin1String(p.id))
			return &p;
	if (id == QLatin1String(kDemo.id))
		return &kDemo;
	return nullptr;
}

// Position in display order: the real ones first, then the test chat; -1 if unknown.
inline int order(const QString &id)
{
	int i = 0;
	for (const Platform &p : kAll) {
		if (id == QLatin1String(p.id))
			return i;
		++i;
	}
	return id == QLatin1String(kDemo.id) ? i : -1;
}

} // namespace platforms
