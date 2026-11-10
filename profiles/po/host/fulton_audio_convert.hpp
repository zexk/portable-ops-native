#pragma once

// MC effects are Ogg Vorbis, not the PSP's ADPCM/ATRAC. Decode only the
// selected cues, then use the SP layout checked by tools/portable-ops/fulton_sfx.py.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "../../../third_party/stb_vorbis.c"

namespace mgs::fulton_audio_convert {
using Bytes = std::vector<std::uint8_t>;
struct Clip { std::vector<std::int16_t> samples; unsigned rate; };

inline std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) throw std::runtime_error("PW sound data is truncated");
    std::uint32_t value;
    std::memcpy(&value, bytes.data() + at, 4);
    return value;
}
inline void append(Bytes &bytes, std::uint32_t value) {
    const auto at = bytes.size();
    bytes.resize(at + 4);
    std::memcpy(bytes.data() + at, &value, 4);
}

// The MC SP header still uses 32-bit offsets. Indexed effects have a
// 16-byte sample prefix; the single streaming balloon cue has 12 bytes.
inline Clip cue(std::span<const std::uint8_t> bank, unsigned index) {
    const std::uint64_t size = word(bank, 4), first = word(bank, 8), map = word(bank, 12);
    if ((word(bank, 0) & 0xffffu) != 0x5053u || size > bank.size() || size < 0x54 || first != 0x30 ||
        (word(bank, size - 4) & 0xffffu) != 0x5045u)
        throw std::runtime_error("Not a Master Collection sound bank");
    std::uint64_t sample, prefix;
    if (map) {
        const std::uint64_t data = word(bank, 16);
        if (map < first || (map - first) % 32 || index >= (map - first) / 32 || data < map || data > size)
            throw std::runtime_error("PW sound bank lacks a cue");
        const std::uint64_t key = word(bank, first + index * 32ull + 8);
        if (key % 8 || key + 8 > data - map) throw std::runtime_error("Invalid PW sound index");
        sample = data + word(bank, map + key);
        prefix = 16;
    } else {
        if (index || word(bank, 20) != 0x50) throw std::runtime_error("Unsupported PW streaming cue");
        sample = 0x50;
        prefix = 12;
    }
    const std::uint64_t length = word(bank, sample);
    if (!length || length > 4u << 20 || sample + prefix + length > size - 4 ||
        word(bank, sample + prefix) != 0x5367674fu)
        throw std::runtime_error("PW sound cue is not bounded Ogg Vorbis");
    int error = 0;
    std::unique_ptr<stb_vorbis, decltype(&stb_vorbis_close)> decoder(
        stb_vorbis_open_memory(bank.data() + sample + prefix, int(length), &error, nullptr), stb_vorbis_close);
    if (!decoder) throw std::runtime_error("Cannot decode PW Ogg Vorbis cue");
    const auto info = stb_vorbis_get_info(decoder.get());
    const auto frames = stb_vorbis_stream_length_in_samples(decoder.get());
    if (info.channels != 1 || info.sample_rate < 8000 || info.sample_rate > 48000 || !frames ||
        frames > info.sample_rate * 15u)
        throw std::runtime_error("Unsupported PW sound channels, rate or duration");
    Clip out{std::vector<std::int16_t>(frames), info.sample_rate};
    if (stb_vorbis_get_samples_short_interleaved(decoder.get(), 1, out.samples.data(), int(frames)) != int(frames) ||
        stb_vorbis_get_error(decoder.get()) != VORBIS__no_error)
        throw std::runtime_error("Truncated PW Ogg Vorbis cue");
    return out;
}

inline std::vector<std::int16_t> resample(const Clip &clip, unsigned rate) {
    if (clip.samples.empty() || !clip.rate || !rate) throw std::runtime_error("Empty PW sound cue");
    if (rate == clip.rate) return clip.samples;
    const auto count = (std::uint64_t(clip.samples.size()) * rate + clip.rate / 2) / clip.rate;
    std::vector<std::int16_t> out(count);
    // Average each output interval when reducing the rate. This also filters
    // high frequencies, instead of aliasing them by dropping input samples.
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double start = double(i) * clip.rate / rate;
        const double end = std::min(double(i + 1) * clip.rate / rate, double(clip.samples.size()));
        double sum = 0;
        for (std::size_t j = std::size_t(start); double(j) < end; ++j)
            sum += clip.samples[j] * (std::min(end, double(j + 1)) - std::max(start, double(j)));
        out[i] = std::int16_t(std::lround(sum / (end - start)));
    }
    return out;
}

// Same predictor search and rounding as fulton_sfx.py; history crosses blocks.
inline Bytes adpcm(std::span<const std::int16_t> samples) {
    if (samples.empty()) throw std::runtime_error("Empty PW sound cue");
    constexpr int coefficients[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
    Bytes out;
    int history1 = 0, history2 = 0;
    for (std::size_t at = 0; at < samples.size(); at += 28) {
        std::array<int, 28> block{};
        for (std::size_t i = 0; i < 28 && at + i < samples.size(); ++i) block[i] = samples[at + i];
        std::uint64_t best = std::numeric_limits<std::uint64_t>::max();
        std::array<std::uint8_t, 16> encoded{};
        int next1 = 0, next2 = 0;
        for (unsigned predictor = 0; predictor < 5; ++predictor) {
            const auto [a, b] = coefficients[predictor];
            int h1 = history1, h2 = history2, peak = 0, shift = 0;
            for (int sample : block) {
                peak = std::max(peak, std::abs(sample - ((a * h1 + b * h2) >> 6)));
                h2 = h1; h1 = sample;
            }
            while (shift < 12 && peak <= 7 * (1 << (11 - shift))) ++shift;
            for (int candidate = std::max(0, shift - 1); candidate <= std::min(12, shift + 1); ++candidate) {
                const int step = 1 << (12 - candidate);
                h1 = history1; h2 = history2;
                std::uint64_t error = 0;
                std::array<std::uint8_t, 16> trial{};
                trial[0] = std::uint8_t(predictor << 4 | candidate);
                trial[1] = at + 28 >= samples.size();
                for (unsigned i = 0; i < 28; ++i) {
                    const int predicted = (a * h1 + b * h2) >> 6;
                    const int q = std::clamp((block[i] - predicted + step / 2) >> (12 - candidate), -8, 7);
                    const int decoded = std::clamp(q * step + predicted, -32768, 32767);
                    const std::int64_t delta = block[i] - decoded;
                    error += delta * delta;
                    h2 = h1; h1 = decoded;
                    trial[2 + i / 2] |= std::uint8_t((q & 15) << ((i % 2) * 4));
                }
                if (error < best) { best = error; encoded = trial; next1 = h1; next2 = h2; }
            }
        }
        out.insert(out.end(), encoded.begin(), encoded.end());
        history1 = next1; history2 = next2;
    }
    return out;
}

inline Bytes bank(const std::vector<Clip> &clips, std::uint16_t id, unsigned rate, std::uint32_t limit) {
    if (clips.empty() || clips.size() > 4 || rate < 8000 || rate > 44100)
        throw std::runtime_error("Invalid converted PW sound bank");
    const auto count = std::uint32_t(clips.size()), table = 0x30 + count * 32, data = table + count * 8;
    Bytes records, keys, payload;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto samples = resample(clips[i], rate);
        const auto encoded = adpcm(samples);
        for (auto value : {0x407fu, 0x01010000u, i * 8, 0u, 0u, 0u,
                           std::uint32_t((samples.size() * 1000ull + rate / 2) / rate), 0u}) append(records, value);
        append(keys, std::uint32_t(payload.size())); append(keys, 0);
        // Low word is the playback pitch, 0x1000 at 44.1 kHz (PW grunt: 15,750 Hz, 0x5B6).
        append(payload, std::uint32_t(encoded.size())); append(payload, rate << 16 | rate * 0x1000u / 44100u);
        payload.insert(payload.end(), encoded.begin(), encoded.end());
    }
    const auto voice = (data + std::uint32_t(payload.size()) + 15) & ~15u;
    const auto size = (voice + 24 + 4 + 0x7ffu) & ~0x7ffu;
    if (size > limit) throw std::runtime_error("PW sounds do not fit PO's bank");
    Bytes out;
    for (auto value : {0x5053u | std::uint32_t(id) << 16, size, 0x30u, table, data, voice, voice, voice + 24})
        append(out, value);
    out.resize(0x30);
    for (const auto *part : {&records, &keys, &payload}) out.insert(out.end(), part->begin(), part->end());
    out.resize(voice);
    for (auto value : {0x407fu, 0x1fe080ffu, 0u, 0x10003c00u, 0u, 0u}) append(out, value);
    out.resize(size - 4);
    append(out, 0x5045u | std::uint32_t(id) << 16);
    return out;
}
} // namespace mgs::fulton_audio_convert
