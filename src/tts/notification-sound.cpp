#include "notification-sound.hpp"

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tts {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kPeak = 0.25f; // -12 dBFS
constexpr uint64_t kNsPerMs = 1000000ULL;

// One tone: a linear attack, an exponential decay and a linear release so it never clicks.
void addTone(PcmBuffer &out, std::size_t start, std::size_t frames, double hz)
{
	const double rate = out.sampleRate;
	const std::size_t attack = std::size_t(rate * 0.008);
	const std::size_t release = std::min(frames, std::size_t(rate * 0.020));
	const double tau = rate * 0.060;
	for (std::size_t i = 0; i < frames && start + i < out.samples.size(); ++i) {
		double envelope = i < attack ? double(i) / double(attack) : std::exp(-double(i - attack) / tau);
		if (i >= frames - release)
			envelope *= double(frames - i) / double(release);
		out.samples[start + i] = float(kPeak * envelope * std::sin(2.0 * kPi * hz * double(i) / rate));
	}
}

uint32_t le32(const char *p)
{
	return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
}

uint16_t le16(const char *p)
{
	return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(p));
}

} // namespace

PcmBuffer notificationChime(uint32_t sampleRate)
{
	PcmBuffer out;
	if (!sampleRate)
		return out;
	out.sampleRate = sampleRate;
	out.channels = 1;
	const std::size_t total = std::size_t(std::lround(sampleRate * 0.250));
	const std::size_t first = std::size_t(std::lround(sampleRate * 0.120));
	out.samples.assign(total, 0.0f);
	addTone(out, 0, first, 880.0);         // A5
	addTone(out, first, total - first, 1318.51); // E6
	return out;
}

PcmBuffer parseWav(const QByteArray &data, double maxSeconds)
{
	PcmBuffer out;
	const qsizetype size = data.size();
	if (size < 12 || std::memcmp(data.constData(), "RIFF", 4) != 0 || std::memcmp(data.constData() + 8, "WAVE", 4) != 0)
		return out;
	uint16_t channels = 0, bits = 0, format = 0;
	uint32_t rate = 0;
	bool haveFormat = false;
	qsizetype offset = 12;
	while (offset + 8 <= size) {
		const char *chunk = data.constData() + offset;
		const uint32_t chunkSize = le32(chunk + 4);
		const qsizetype body = offset + 8;
		if (chunkSize > uint32_t(size - body))
			return out; // declares more than the file holds
		if (std::memcmp(chunk, "fmt ", 4) == 0) {
			if (chunkSize < 16)
				return out;
			format = le16(chunk + 8);
			channels = le16(chunk + 10);
			rate = le32(chunk + 12);
			bits = le16(chunk + 22);
			haveFormat = true;
		} else if (std::memcmp(chunk, "data", 4) == 0) {
			if (!haveFormat || format != 1 || bits != 16 || (channels != 1 && channels != 2) || !rate ||
			    rate > 384000)
				return out;
			const std::size_t frameBytes = std::size_t(channels) * 2;
			const std::size_t frames = chunkSize / frameBytes;
			if (!frames || double(frames) / double(rate) > maxSeconds)
				return out;
			out.sampleRate = rate;
			out.channels = channels;
			out.samples.reserve(frames * channels);
			const char *samples = data.constData() + body;
			for (std::size_t i = 0; i < frames * channels; ++i)
				out.samples.push_back(sampleToFloat(int16_t(le16(samples + i * 2))));
			return out;
		}
		offset = body + qsizetype(chunkSize) + qsizetype(chunkSize & 1u); // chunks are padded to even sizes
	}
	return out; // no data chunk
}

PcmBuffer resamplePcm(const PcmBuffer &in, uint32_t sampleRate)
{
	PcmBuffer out;
	if (!sampleRate || !in.sampleRate || !in.channels || in.samples.empty())
		return out;
	if (sampleRate == in.sampleRate)
		return in;
	const std::size_t inFrames = in.frames();
	const std::size_t outFrames = std::size_t(std::llround(double(inFrames) * double(sampleRate) / double(in.sampleRate)));
	out.sampleRate = sampleRate;
	out.channels = in.channels;
	out.samples.reserve(outFrames * in.channels);
	const double step = double(in.sampleRate) / double(sampleRate);
	for (std::size_t i = 0; i < outFrames; ++i) {
		const double pos = double(i) * step;
		const std::size_t index = std::min(inFrames - 1, std::size_t(pos));
		const std::size_t next = std::min(inFrames - 1, index + 1);
		const float frac = float(pos - double(index));
		for (uint32_t c = 0; c < in.channels; ++c) {
			const float a = in.samples[index * in.channels + c];
			const float b = in.samples[next * in.channels + c];
			out.samples.push_back(a + (b - a) * frac);
		}
	}
	return out;
}

bool chimeAllowed(uint64_t nowNs, uint64_t lastChimeNs, int cooldownMs)
{
	if (lastChimeNs == 0 || cooldownMs <= 0)
		return true;
	if (nowNs < lastChimeNs)
		return false;
	return nowNs - lastChimeNs >= uint64_t(cooldownMs) * kNsPerMs;
}

} // namespace tts
