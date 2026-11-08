#include "overlay-presets.hpp"

#include <cstring>

namespace overlay {

const char *const kDefaultFontStack = "\"Segoe UI\", system-ui, -apple-system, \"Helvetica Neue\", Roboto, sans-serif";

namespace {

// Cuts s to at most max bytes without splitting a UTF-8 character.
std::string truncateUtf8(const std::string &s, size_t max)
{
	if (s.size() <= max)
		return s;
	size_t end = max;
	// Back up over continuation bytes (10xxxxxx) to the start of the cut character.
	while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80)
		--end;
	return s.substr(0, end);
}

std::string trim(const std::string &s)
{
	const size_t first = s.find_first_not_of(' ');
	if (first == std::string::npos)
		return {};
	return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

} // namespace

std::string cssColor(uint32_t abgr, bool withAlpha)
{
	const unsigned r = abgr & 0xff, g = (abgr >> 8) & 0xff, b = (abgr >> 16) & 0xff;
	const unsigned a = withAlpha ? (abgr >> 24) & 0xff : 0xff;
	// Thousandths with integers: printf would use the locale's decimal separator.
	const unsigned thousandths = (a * 1000 + 127) / 255;
	std::string alpha;
	if (thousandths >= 1000) {
		alpha = "1";
	} else if (thousandths == 0) {
		alpha = "0";
	} else {
		char digits[4];
		digits[0] = char('0' + thousandths / 100);
		digits[1] = char('0' + thousandths / 10 % 10);
		digits[2] = char('0' + thousandths % 10);
		digits[3] = '\0';
		size_t len = 3;
		while (len > 1 && digits[len - 1] == '0')
			--len;
		alpha = "0." + std::string(digits, len);
	}
	return "rgba(" + std::to_string(r) + ", " + std::to_string(g) + ", " + std::to_string(b) + ", " + alpha + ")";
}

std::string cssFontFamily(const std::string &face)
{
	std::string clean;
	for (char c : face) {
		const auto u = static_cast<unsigned char>(c);
		if (u < 0x20 || u == 0x7f || std::strchr(";{}<>", c))
			continue;
		clean += c;
	}
	clean = trim(truncateUtf8(trim(clean), 128));
	if (clean.empty())
		return kDefaultFontStack;
	std::string quoted = "\"";
	for (char c : clean) {
		if (c == '"' || c == '\\')
			quoted += '\\';
		quoted += c;
	}
	return quoted + "\", " + kDefaultFontStack;
}

} // namespace overlay
