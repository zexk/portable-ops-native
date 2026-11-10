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
        mem.store32(shadow_fade, std::bit_cast<std::uint32_t>(50.0f * static_cast<float>(percent)));
        mem.store32(shadow_fade + 4u, std::bit_cast<std::uint32_t>(100.0f * static_cast<float>(percent)));
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
