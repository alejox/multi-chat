#pragma once

// Pure helpers for the overlay sources: CSS values built from OBS settings. No OBS or Qt
// types here, so the tests can use them without linking libobs.

#include <cstdint>
#include <string>

namespace overlay {

// Font stack the pages use when no family is chosen (and as fallback after a chosen one).
extern const char *const kDefaultFontStack;

// "rgba(r, g, b, a)" from an OBS color (0xAABBGGRR). Without alpha the color is always
// opaque: OBS color properties without an alpha channel do not guarantee the top byte.
std::string cssColor(uint32_t abgr, bool withAlpha);

// CSS font-family value for a font face name: the face quoted (quotes and backslashes
// escaped, `;{}<>` and control characters removed, at most 128 bytes) followed by the
// default stack. An empty face gives the default stack alone.
std::string cssFontFamily(const std::string &face);

} // namespace overlay
