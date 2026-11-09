#pragma once

#include <cstdint>
#include <vector>

namespace mgs::fulton_audio_catalog {
// PO ULUS-10202 1.02 SHDS: append one private bank and four sound ids.
// Existing ids, live bank pointers and slot.dat offsets keep their values.
constexpr std::uint32_t kBank = 324, kFirstSound = 2971, kSounds = 4, kCapacity = 0x9000;

template<class Memory>
std::vector<std::uint8_t> extend(const Memory &memory, std::uint32_t source, std::uint32_t destination) {
    if (!memory.contains(source, 0x34) || memory.load32(source) != 0x53444853u ||
        memory.load32(source + 0xcu) != 0x0a570029u || memory.load32(source + 0x10u) != 283u)
        return {};
    constexpr std::uint32_t sounds = kFirstSound + kSounds, banks = kBank + 1;
    constexpr std::uint32_t cue = 0x34, bank = cue + sounds * 8;
    constexpr std::uint32_t slot = bank + banks * 16, first = slot + banks * 4;
    std::vector<std::uint8_t> output(first + banks * 2);
    auto put = [&](std::uint32_t at, std::uint32_t value, unsigned bytes = 4) {
        for (unsigned i = 0; i < bytes; ++i) output[at + i] = value >> (i * 8);
    };
    auto copy = [&](std::uint32_t to, std::uint32_t from, std::uint32_t size) {
        if (!memory.contains(from, size)) return false;
        for (std::uint32_t i = 0; i < size; ++i) output[to + i] = memory.load8(from + i);
        return true;
    };
    if (!copy(0, source, 0x34) ||
        !copy(cue, memory.load32(source + 0x14u), kFirstSound * 8) ||
        !copy(bank, memory.load32(source + 0x18u), kBank * 16) ||
        !copy(slot, memory.load32(source + 0x1cu), kBank * 4) ||
        !copy(first, memory.load32(source + 0x20u), kBank * 2)) return {};
    // The initializer counts reserved banks + banks + sounds. Adding a bank
    // already adds one entry, hence three more here for four new sound ids.
    put(0xcu, 0x0029u | ((2647u + kSounds - 1) << 16));
    put(0x10u, 284u);
    put(0x14u, destination + cue);
    put(0x18u, destination + bank);
    put(0x1cu, destination + slot);
    put(0x20u, destination + first);
    for (unsigned i = 0; i < kSounds; ++i) {
        put(cue + (kFirstSound + i) * 8, 0x40010103u); // ordinary ADPCM effect
        put(cue + (kFirstSound + i) * 8 + 4, 0);
    }
    put(bank + kBank * 16 + 8, kCapacity);
    put(slot + kBank * 4, 0xffffffffu); // private bank has no disc payload
    put(first + kBank * 2, kFirstSound, 2);
    return output;
}
} // namespace mgs::fulton_audio_catalog
