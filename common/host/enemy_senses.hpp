#pragma once

// Game > Difficulty > Enemy senses (PO ULUS-10202 1.02; PO+ is not wired: its
// soldiers are another size and their factory is not found by this shape).
//
// A soldier perceives through two components of its stage's sensor code: a
// vision cone and a hearing sphere, pointed to from its work (actor + 0x40)
// at +0x1B98 and +0x1B9C. The cone's sight distance is the int at +0x48
// (option 'i', 20000 when absent; an area manager may set it again from a
// table), its half-angles the s16 at +0x50 (sideways) and +0x54 (up and
// down), 0x2000 (45 degrees) by default; the AI keeps its own adjustments
// beside them (+0x4C, +0x52, +0x56). The sphere's radius is the int at +0x44
// (option 'h', 10000 when absent), its adjustment at +0x48; the hearing check
// doubles or caps it by the kind of noise. Work +0x194C holds the base sight
// distance (option 'd', else a copy of +0x48).
//
// Sharper senses scale those bases for the length of each soldier's update,
// where its sight and hearing are checked, and put them back afterwards: the
// game never keeps a scaled value, so its own rewrites and the readers outside
// the update see what they always did. A value the update itself changed is
// left as the update left it.
//
// Each of the 55 PO stage modules with soldiers has its own copy of their
// code at its own place. The soldier factory is found by its shape and the
// work it allocates (0x1C10); the update it gives the actor is wrapped.

#include "guest_wrap.hpp"
#include "install/user_data.hpp"
#include "portablekit/hle_extension.hpp"
#include "psprecomp/runtime.hpp"

#if defined(PORTABLEKIT_HAS_RENDERER)
#include "ui/widgets.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace mgs::enemy_senses {

struct Level {
    const char *name;
    float sight, angle, hearing;
};
// Taste, not measured against MGS3's difficulties.
inline constexpr Level kLevels[] = {{"Normal", 1.0f, 1.0f, 1.0f}, {"Hard", 1.5f, 1.25f, 1.5f},
                                    {"Extreme", 2.0f, 1.5f, 2.0f}};
inline int level = 0;
inline bool loaded = false;
inline GuestWrap update;

// The factory's first 21 instructions, as code_shape (camera.hpp) has them but
// with constants loaded from $zero kept: the work size 0x1C10 tells the
// soldier's factory from the other actors' made the same way. The update's
// address is formed in $t0 at words 12 (lui) and 15 (addiu).
inline constexpr std::uint32_t kFactoryShape[] = {
    0x27bd0000u, 0x3c060000u, 0x34c60000u, 0x3c070000u, 0xafb20000u, 0xafb10000u, 0xafb00000u,
    0x00008021u, 0x00008821u, 0xafbf0000u, 0x0c000000u, 0x24041c10u, 0x3c080000u, 0x3c090000u,
    0x00409021u, 0x25080000u, 0x25290000u, 0x3c060000u, 0x34c60000u, 0x3c070000u, 0x1040000cu};
// The update: work = actor + 0x40, passed on to the soldier's own update.
inline constexpr std::uint32_t kUpdateShape[] = {0x27bdfff0u, 0xafbf0000u, 0x0c000000u, 0x24840040u};

[[nodiscard]] constexpr std::uint32_t factory_shape(std::uint32_t word) {
    const std::uint32_t op = word >> 26u;
    if (op == 0x09u && ((word >> 21u) & 31u) == 0u) return word;
    if (op == 0x02u || op == 0x03u) return word & 0xfc000000u;
    switch (op) {
    case 0x08: case 0x09: case 0x0d: case 0x0f: case 0x20: case 0x21: case 0x23: case 0x24: case 0x25:
    case 0x28: case 0x29: case 0x2b: case 0x31: case 0x32: case 0x36: case 0x39: case 0x3a: case 0x3e:
        return word & 0xffff0000u;
    default: return word;
    }
}

inline void load() {
    if (loaded) return;
    loaded = true;
    try {
        const auto entries = portablekit::install::read_settings_file(portablekit::install::user_data_directory());
        if (const auto found = entries.find("game.enemy_senses"); found != entries.end())
            for (int i = 0; i < 3; ++i)
                if (found->second == kLevels[i].name) level = i;
    } catch (const std::exception &) {
    }
    // <prefix>_ENEMY_SENSES (0 to 2) decides for one run.
    if (const char *text = portablekit::hle_extension::env("ENEMY_SENSES"); text && *text >= '0' && *text <= '2')
        level = *text - '0';
}

inline void save() {
    try {
        const auto root = portablekit::install::user_data_directory();
        auto entries = portablekit::install::read_settings_file(root);
        entries["game.enemy_senses"] = kLevels[level].name;
        portablekit::install::write_settings_file(root, entries);
    } catch (const std::exception &error) {
        std::cerr << "[senses] cannot save setting: " << error.what() << std::endl;
    }
}

// A field scaled for one update: where, what the game had, what was written.
struct Field {
    std::uint32_t at = 0;
    std::int32_t was = 0, now = 0;
    bool half = false;
};

inline Field scale(psprecomp::GuestMemory &memory, std::uint32_t at, float factor, bool half) {
    const std::int32_t was = half ? static_cast<std::int16_t>(memory.load16(at)) : static_cast<std::int32_t>(memory.load32(at));
    if (was <= 0 || factor == 1.0f) return {};
    // A half-angle stops at 180 degrees; a distance far short of overflowing.
    const double limit = half ? 0x7fff : 0x3fffffff;
    const auto now = static_cast<std::int32_t>(std::min(static_cast<double>(was) * factor, limit));
    if (half) memory.store16(at, static_cast<std::uint16_t>(now));
    else memory.store32(at, static_cast<std::uint32_t>(now));
    return {at, was, now, half};
}

inline void soldier_update(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    if (update.inner || level == 0) {
        if (update.inner) update.around(rt, ctx, {});
        else update.original(rt, ctx);
        return;
    }
    auto &memory = static_cast<psprecomp::Runtime &>(rt).memory();
    const Level &with = kLevels[level];
    const std::uint32_t work = ctx.gpr[4] + 0x40u;
    std::array<Field, 5> fields{};
    if (memory.contains(work, 0x1BA0u)) {
        const std::uint32_t vision = memory.load32(work + 0x1B98u), hearing = memory.load32(work + 0x1B9Cu);
        if (memory.contains(vision, 0x58u)) {
            fields[0] = scale(memory, vision + 0x48u, with.sight, false);
            fields[1] = scale(memory, vision + 0x50u, with.angle, true);
            fields[2] = scale(memory, vision + 0x54u, with.angle, true);
            fields[3] = scale(memory, work + 0x194Cu, with.sight, false);
        }
        if (memory.contains(hearing, 0x48u)) fields[4] = scale(memory, hearing + 0x44u, with.hearing, false);
    }
    // <prefix>_TRACE_SENSES: the first scalings, as the game had them and as written.
    static const bool trace = portablekit::hle_extension::env("TRACE_SENSES") != nullptr;
    static int traced = 0;
    if (trace && fields[0].at != 0u && traced < 12) {
        ++traced;
        std::cout << "[senses] soldier 0x" << std::hex << work << std::dec;
        for (const Field &field : fields)
            if (field.at != 0u) std::cout << ' ' << field.was << "->" << field.now;
        std::cout << std::endl;
    }
    update.around(rt, ctx, [fields](psprecomp::Runtime &runtime, std::uint32_t) {
        auto &m = runtime.memory();
        for (const Field &field : fields) {
            if (field.at == 0u) continue;
            if (field.half) {
                if (static_cast<std::int16_t>(m.load16(field.at)) == field.now)
                    m.store16(field.at, static_cast<std::uint16_t>(field.was));
            } else if (static_cast<std::int32_t>(m.load32(field.at)) == field.now) {
                m.store32(field.at, static_cast<std::uint32_t>(field.was));
            }
        }
    });
}

// A stage module is in place: wrap its soldiers' update, while senses are
// sharper. A change of level applies from the next area loaded.
inline void module_ready(psprecomp::Runtime &runtime, std::string_view name, std::uint32_t base,
                         std::span<const std::uint8_t> code) {
    load();
    if (level == 0 || name != "mgp_stage") return;
    const auto word = [&code](std::size_t at) {
        std::uint32_t value = 0u;
        std::memcpy(&value, code.data() + at, sizeof(value));
        return value;
    };
    constexpr std::size_t kLength = std::size(kFactoryShape);
    std::size_t found = 0u, count = 0u;
    for (std::size_t at = 0; at + 4u * kLength <= code.size() && count < 2u; at += 4u) {
        std::size_t k = 0;
        while (k < kLength && factory_shape(word(at + 4u * k)) == kFactoryShape[k]) ++k;
        if (k == kLength) found = at, ++count;
    }
    if (count != 1u) return;  // no soldiers here, or not this code
    const std::uint32_t hi = word(found + 4u * 12u) & 0xffffu, lo = word(found + 4u * 15u) & 0xffffu;
    const std::uint32_t at = (hi << 16u) + static_cast<std::uint32_t>(static_cast<std::int16_t>(lo));
    if (at < base || at - base + sizeof(kUpdateShape) > code.size()) return;
    for (std::size_t k = 0; k < std::size(kUpdateShape); ++k)
        if ((k == 2u ? word(at - base + 4u * k) & 0xfc000000u : word(at - base + 4u * k)) != kUpdateShape[k]) return;
    update.inner = false;
    if (update.install(runtime, at, &soldier_update, "mgs_soldier_update"))
        std::cout << "[senses] " << kLevels[level].name << ": soldier update at 0x" << std::hex << at << std::dec
                  << std::endl;
}

inline void reset() {
    level = 0;
    save();
}

inline void draw() {
#if defined(PORTABLEKIT_HAS_RENDERER)
    using namespace portablekit::ui;
    load();
    section("Difficulty");
    if (const int delta = choice_row(
            "Enemy senses", kLevels[level].name,
            {false, {},
             "How far enemy soldiers see and hear, and how wide they look. Hard: half as far again and a quarter "
             "wider. Extreme: twice as far and half again as wide. Applies when the next area loads."})) {
        level = (level + delta + 3) % 3;
        save();
    }
#endif
}

} // namespace mgs::enemy_senses
