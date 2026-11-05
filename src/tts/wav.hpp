#pragma once

// Parseo de un RIFF/WAVE (PCM o IEEE float) a PcmBuffer. Lo usa el sintetizador de Windows, que
// renderiza SAPI a un WAV en memoria; queda aquí, independiente del sistema, para poder probarlo.

#include "pcm.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace tts {

namespace {

inline uint16_t wavLe16(const uint8_t *p)
{
	return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

inline uint32_t wavLe32(const uint8_t *p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline uint64_t wavLe64(const uint8_t *p)
{
	return uint64_t(wavLe32(p)) | (uint64_t(wavLe32(p + 4)) << 32);
}

// Convierte una muestra little-endian de `bits` bits a float. `isFloat` distingue PCM de IEEE.
inline float wavSample(const uint8_t *p, uint16_t bits, bool isFloat)
{
	if (isFloat) {
		if (bits == 32) {
			float f;
			const uint32_t u = wavLe32(p);
			std::memcpy(&f, &u, sizeof f);
			return f;
		}
		if (bits == 64) {
			double d;
			const uint64_t u = wavLe64(p);
			std::memcpy(&d, &u, sizeof d);
			return static_cast<float>(d);
		}
		return 0.0f;
	}
	switch (bits) {
	case 8: // PCM sin signo
		return (static_cast<float>(p[0]) - 128.0f) / 128.0f;
	case 16:
		return sampleToFloat(static_cast<int16_t>(wavLe16(p)));
	case 24: {
		int32_t v = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
		if (v & 0x800000)
			v |= ~0xFFFFFF; // extiende el signo
		return static_cast<float>(v) / 8388608.0f;
	}
	case 32:
		return sampleToFloat(static_cast<int32_t>(wavLe32(p)));
	default:
		return 0.0f;
	}
}

} // namespace

inline PcmBuffer fromWav(const uint8_t *data, std::size_t size)
{
	PcmBuffer out;
	if (!data || size < 12 || data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F' ||
	    data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E')
		return out;

	bool haveFmt = false;
	bool haveData = false;
	uint16_t formatTag = 0;
	uint16_t channels = 0;
	uint16_t bits = 0;
	uint32_t sampleRate = 0;
	const uint8_t *dataPtr = nullptr;
	std::size_t dataBytes = 0;

	std::size_t pos = 12;
	while (pos + 8 <= size) {
		const uint8_t *id = data + pos;
		const std::size_t chunkSize = wavLe32(data + pos + 4);
		const std::size_t remaining = size - pos - 8;
		if (chunkSize > remaining)
			break; // chunk truncado: los tamaños declarados ya no son de fiar
		const uint8_t *body = data + pos + 8;

		if (id[0] == 'f' && id[1] == 'm' && id[2] == 't' && id[3] == ' ') {
			if (chunkSize >= 16) {
				formatTag = wavLe16(body);
				channels = wavLe16(body + 2);
				sampleRate = wavLe32(body + 4);
				bits = wavLe16(body + 14);
				haveFmt = true;
			}
		} else if (id[0] == 'd' && id[1] == 'a' && id[2] == 't' && id[3] == 'a') {
			dataPtr = body;
			dataBytes = chunkSize;
			haveData = true;
		}

		pos += 8 + chunkSize + (chunkSize & 1); // los chunks van alineados a 2 bytes
	}

	if (!haveFmt || !haveData || !channels || !sampleRate || !dataBytes)
		return out;
	const bool isFloat = formatTag == 3;
	const bool isPcm = formatTag == 1;
	if (!isFloat && !isPcm)
		return out;
	if (isFloat && bits != 32 && bits != 64)
		return out;
	if (isPcm && (bits != 8 && bits != 16 && bits != 24 && bits != 32))
		return out;

	const uint32_t bytesPerSample = bits / 8;
	const uint32_t frameBytes = channels * bytesPerSample;
	if (!frameBytes)
		return out;
	const std::size_t frames = dataBytes / frameBytes;

	out.sampleRate = sampleRate;
	out.channels = channels > 2 ? 1 : channels;
	out.samples.reserve(frames * out.channels);
	for (std::size_t f = 0; f < frames; ++f) {
		const uint8_t *frame = dataPtr + f * frameBytes;
		if (channels > 2) {
			float mix = 0.0f;
			for (uint32_t c = 0; c < channels; ++c)
				mix += wavSample(frame + c * bytesPerSample, bits, isFloat);
			out.samples.push_back(mix / static_cast<float>(channels));
		} else {
			for (uint32_t c = 0; c < channels; ++c)
				out.samples.push_back(wavSample(frame + c * bytesPerSample, bits, isFloat));
		}
	}
	return out;
}

} // namespace tts
