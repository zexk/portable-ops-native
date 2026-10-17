// Key table for the retail UMD executable tag 0xC0CB167C, which both
// Portable Ops and Portable Ops+ use. This tag selects the older "~PSP" header
// layout with a published 0x90-byte table instead of a 16-byte key. Checked:
// each EBOOT.BIN decrypts byte for byte to the unencrypted BOOT.BIN on its disc.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mgs {

inline constexpr std::uint32_t kUmdTag = 0xC0CB167Cu;

inline constexpr std::array<std::uint32_t, 36> kUmdKeyTableWords = {
    0xDA8E36FAu, 0x5DD97447u, 0x76C19874u, 0x97E57EAFu, 0x1CAB09BDu, 0x9835BAC6u,
    0x03D39281u, 0x03B205CFu, 0x2882E734u, 0xE714F663u, 0xB96E2775u, 0xBD8AAFC7u,
    0x1DD3EC29u, 0xECA4A16Cu, 0x5F69EC87u, 0x85981E92u, 0x7CFCAE21u, 0xBAE9DD16u,
    0xE6A97804u, 0x2EEE02FCu, 0x61DF8A3Du, 0xDD310564u, 0x9697E149u, 0xC2453F3Bu,
    0xF91D8456u, 0x39DA6BC8u, 0xB3E5FEF5u, 0x89C593A3u, 0xFB5C8ABCu, 0x6C0B7212u,
    0xE10DD3CBu, 0x98D0B2A8u, 0x5FD61847u, 0xF0DC2357u, 0x7701166Au, 0x0F5C3B68u,
};

constexpr std::array<std::uint8_t, 0x90> umd_key_table() {
    std::array<std::uint8_t, 0x90> bytes{};
    for (std::size_t i = 0; i < kUmdKeyTableWords.size(); ++i)
        for (std::size_t b = 0; b < 4; ++b)
            bytes[i * 4 + b] = static_cast<std::uint8_t>(kUmdKeyTableWords[i] >> (8 * b));
    return bytes;
}

inline constexpr std::array<std::uint8_t, 0x90> kUmdKeyTable = umd_key_table();

} // namespace mgs
