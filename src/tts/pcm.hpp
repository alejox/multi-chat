#pragma once

// Audio ya normalizado: float de 32 bits intercalado, mono o estéreo. El sintetizador de cada
// sistema entrega su propio formato; aquí se convierte para que el resto no tenga que saberlo.

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace tts {

struct PcmBuffer {
	uint32_t sampleRate = 0;
	uint32_t channels = 0;
	std::vector<float> samples; // intercalado: L R L R...

	std::size_t frames() const { return channels ? samples.size() / channels : 0; }
};

template<typename T> float sampleToFloat(T sample)
{
	if constexpr (std::is_floating_point_v<T>)
		return static_cast<float>(sample);
	else if constexpr (sizeof(T) == 2)
		return static_cast<float>(sample) / 32768.0f;
	else
		return static_cast<float>(static_cast<double>(sample) / 2147483648.0);
}

// planes: un puntero por canal o, con interleaved, uno solo con todos los canales seguidos.
// Más de dos canales se mezclan a mono: OBS acepta otras disposiciones, pero una voz no las usa.
// Con datos inválidos devuelve un búfer vacío.
template<typename T>
PcmBuffer toFloatPcm(const T *const *planes, std::size_t planeCount, std::size_t frames, uint32_t channels,
		     bool interleaved, uint32_t sampleRate)
{
	PcmBuffer out;
	if (!planes || !frames || !channels || !sampleRate || planeCount != (interleaved ? 1u : channels))
		return out;
	for (std::size_t i = 0; i < planeCount; ++i)
		if (!planes[i])
			return out;

	const auto at = [&](std::size_t frame, uint32_t channel) -> float {
		return sampleToFloat(interleaved ? planes[0][frame * channels + channel] : planes[channel][frame]);
	};
	out.sampleRate = sampleRate;
	out.channels = channels > 2 ? 1 : channels;
	out.samples.reserve(frames * out.channels);
	for (std::size_t frame = 0; frame < frames; ++frame) {
		if (channels > 2) {
			float mix = 0.0f;
			for (uint32_t c = 0; c < channels; ++c)
				mix += at(frame, c);
			out.samples.push_back(mix / static_cast<float>(channels));
		} else {
			for (uint32_t c = 0; c < channels; ++c)
				out.samples.push_back(at(frame, c));
		}
	}
	return out;
}

} // namespace tts
