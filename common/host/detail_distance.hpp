#pragma once

// Detail distance: how far away soldiers keep their full model.
//
// Stage scripts register LOD records with GCL command 0xC8C4F3, whose handler
// 0x15324 calls 0x15210(key, models, distances). The table at data 0x20D650
// holds a count and up to 64 records of {key, lod0, lod1, d0^2, d1^2}; actors
// copy the distances when they are set up, so the setting applies from the
// next area load. The handler is reached only through the script dispatcher,
// so it can be wrapped (calls inside one generated unit cannot). Records the
// call wrote are scaled; others are left alone.

#include "guest_wrap.hpp"
#include "camera.hpp"
#include "psprecomp/runtime.hpp"
#include "settings/settings.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>

namespace mgs::detail_distance {

constexpr std::uint32_t kRecords = 64u, kStride = 20u;
inline GuestWrap handler;
inline std::uint32_t table = 0;        // guest address of the count
inline std::uint32_t shadow_fade = 0;  // guest address of the fade start, then its end

inline void scale_shadow(psprecomp::GuestMemory &memory) {
    if (!shadow_fade) return;
    const float percent = static_cast<float>(portablekit::settings::current().detail_distance_percent);
    memory.store32(shadow_fade, std::bit_cast<std::uint32_t>(50.0f * percent));
    memory.store32(shadow_fade + 4u, std::bit_cast<std::uint32_t>(100.0f * percent));
}

// PO+ US 1.01 puts contact shadows in stage modules. Match the distance
// clamp and fade arithmetic, masking only relocated address halves.
inline constexpr std::uint32_t kShadowFadeShape[] = {
    0x3c030000u, 0xc7b50200u, 0xc4600000u, 0x4600a83cu, 0x00000000u,
    0x45030008u, 0x46000546u, 0x3c020000u, 0xc4400000u, 0x4615003cu,
    0x00000000u, 0x45000003u, 0xc4610000u, 0x46000546u, 0xc4610000u,
    0x3c020000u, 0xc4420000u, 0x4601a801u, 0x46010003u, 0x46001541u};

inline void shadow_in_module(psprecomp::Runtime &runtime, std::span<const std::uint8_t> code) {
    const auto previous = shadow_fade;
    shadow_fade = 0u; // A menu or a new stage must not retain the old data address.
    const auto found = find_shape(code, kShadowFadeShape, [](std::uint32_t word) {
        return word >> 26u == 15u || (word >> 26u == 49u && (word >> 21u & 31u) != 29u)
                   ? word & 0xffff0000u : word;
    });
    if (!found) return;
    const auto word = [&](unsigned index) {
        std::uint32_t value;
        std::memcpy(&value, code.data() + *found + index * 4u, 4u);
        return value;
    };
    const auto address = [&](unsigned hi, unsigned lo) {
        return ((word(hi) & 0xffffu) << 16u) + static_cast<std::int16_t>(word(lo));
    };
    const auto start = address(0, 2), end = address(7, 8);
    auto &memory = runtime.memory();
    if ((start & 3u) || !memory.contains(start, 12u) || end != start + 4u ||
        address(0, 12) != start || address(0, 14) != start || address(15, 16) != start + 8u)
        return;
    const auto first = memory.load32(start), last = memory.load32(end);
    const bool defaults = first == std::bit_cast<std::uint32_t>(5000.0f) &&
                          last == std::bit_cast<std::uint32_t>(10000.0f);
    // Re-identifying an already installed module must not compound scaling.
    const float first_float = std::bit_cast<float>(first);
    const bool scaled = previous == start && first_float >= 5000.0f && first_float <= 20000.0f &&
                        std::bit_cast<float>(last) == first_float * 2.0f;
    if ((!defaults && !scaled) || memory.load32(start + 8u) != std::bit_cast<std::uint32_t>(1.0f)) return;
    shadow_fade = start;
    scale_shadow(memory);
    std::cout << "[detail] stage shadow fade scaled at 0x" << std::hex << start << std::dec << std::endl;
}

inline void register_lod(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    if (handler.inner) {
        handler.inner = false;
        handler.original(rt, ctx);
        return;
    }
    auto &memory = static_cast<psprecomp::Runtime &>(rt).memory();
    std::array<std::uint32_t, kRecords * 2> before{};
    for (std::uint32_t i = 0; i < kRecords * 2; ++i)
        before[i] = memory.load32(table + 4u + (i / 2) * kStride + 12u + (i % 2) * 4u);
    handler.around(rt, ctx, [before](psprecomp::Runtime &r, std::uint32_t) {
        const std::uint32_t percent = portablekit::settings::current().detail_distance_percent;
        auto &mem = r.memory();
        // Foot shadows fade from 5000 to 10000 units from the camera (the two
        // floats are literals of their update, 0x14F70C, alone); the fade ends
        // at twice its start, so both scale together.
        scale_shadow(mem);
        if (percent == 100u) return;
        const float scale = static_cast<float>(percent * percent) / 10000.0f;  // distances are squared
        const std::uint32_t count = mem.load32(table);
        for (std::uint32_t i = 0; i < kRecords * 2 && i / 2 < count; ++i) {
            const std::uint32_t at = table + 4u + (i / 2) * kStride + 12u + (i % 2) * 4u;
            const std::uint32_t now = mem.load32(at);
            if (now != before[i]) mem.store32(at, std::bit_cast<std::uint32_t>(std::bit_cast<float>(now) * scale));
        }
    });
}

inline void install(psprecomp::Runtime &runtime, std::uint32_t at, std::uint32_t lod_table, std::uint32_t fade) {
    table = lod_table;
    shadow_fade = fade;
    if (handler.install(runtime, at, &register_lod, "mgs_register_lod"))
        std::cout << "[detail] LOD records scaled at 0x" << std::hex << at << std::dec << std::endl;
}

} // namespace mgs::detail_distance
