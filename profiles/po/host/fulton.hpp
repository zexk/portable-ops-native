#pragma once

// Optional recovery for ULUS-10202 1.02. All addresses refer to its decrypted
// executable. Stage helpers are located from their instructions after linking.
#include "portablekit/hle_extension.hpp"
#include "fulton_sound.hpp"
#include "fulton_visuals.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>

namespace mgs::fulton {
constexpr std::uint32_t engine = 0x08804000u;
constexpr std::uint32_t update = engine + 0xc488cu;
// Engine pad bit, not the sceCtrl one (the engine remaps L/R to 0x400/0x800).
constexpr std::uint32_t triangle = 0x1000u;
// Reach, in world units from the player to the soldier, and how squarely the
// player must face him. The game's own grab test (player +0xB98 bit 0x10,
// live ULUS-10202 1.02) sets within about 700 while facing an unconscious
// soldier and clears by 830 or on turning away; Fulton reaches a little
// further, so it measures both itself. The player's yaw is the high half of
// +0x130, 0x10000 a full turn, pointing along (sin, cos) on world X and Z:
// in live samples it matched the direction of travel while running.
// ponytail: both numbers are picked by feel. Replace them with PW's own
// reach and cone once a PW trace of its Fulton prompt test has measured them.
constexpr float reach = 1200.0f, facing = 0.5f; // cosine: within 60 degrees
constexpr std::array<std::uint32_t, 16> notify_shape{
    0x27bdffd0, 0xafbf0020, 0x00803021, 0x03a02821,
    0x8c821438, 0x14400004, 0x00402021, 0x8fbf0020,
    0x03e00008, 0x27bd0030, 0x8cc71850, 0x8cc305cc,
    0x8cc618a0, 0x24020004, 0xa7a20000, 0x27a20010};
constexpr std::array<std::uint32_t, 16> cleanup_shape{
    0x27bdfff0, 0xafbf0008, 0x3c058000, 0xafb10004,
    0x00808821, 0xafb00000, 0x0c00e2b2, 0x8c901bac,
    0x00001021, 0x00001821, 0xae221888, 0xae23188c,
    0x3c050001, 0x8e240010, 0x0c00148e, 0xae2018b8};

constexpr std::uint32_t shape_word(std::uint32_t word) {
    return word >> 26u == 3u ? word & 0xfc000000u : word;
}

template<class Memory>
bool matches(const Memory &memory, std::uint32_t at, std::span<const std::uint32_t> shape) {
    if (!memory.contains(at, shape.size_bytes())) return false;
    for (std::size_t i = 0; i < shape.size(); ++i)
        if (shape_word(memory.load32(at + 4u * i)) != shape_word(shape[i])) return false;
    return true;
}

inline std::optional<std::uint32_t> find(std::span<const std::uint8_t> code,
                                        std::span<const std::uint32_t> shape) {
    std::optional<std::uint32_t> found;
    for (std::size_t at = 0; at + shape.size_bytes() <= code.size(); at += 4u) {
        std::size_t i = 0;
        for (; i < shape.size(); ++i) {
            std::uint32_t word;
            std::memcpy(&word, code.data() + at + i * 4u, 4u);
            if (shape_word(word) != shape_word(shape[i])) break;
        }
        if (i != shape.size()) continue;
        if (found) return std::nullopt;
        found = static_cast<std::uint32_t>(at);
    }
    return found;
}

struct Target { std::uint32_t work{}, name{}; };

// Still the same soldier. After the lift animation his state is no longer the
// unconscious one eligible() wants, so the capture checks identity only.
template<class Memory>
bool same(const Memory &memory, Target target) {
    if (!target.name || !memory.contains(target.work, 0x1bb0u) || memory.load32(target.work) != target.name) return false;
    const auto component = memory.load32(target.work + 0x10u);
    return memory.contains(component, 0x28u) && memory.load32(component + 4u) == target.name;
}
template<class Memory>
bool held_up(const Memory &memory, Target target) {
    // Live st01t2 trace: prone hold-up uses flag 0x40 and motion state
    // 0x152 (enemy clip 0xB4), without either unconscious status bit.
    return (memory.load32(target.work + 0x1858u) & 0x40u) != 0u &&
           (memory.load32(target.work + 0x3c0u) & 0xffffu) == 0x152u;
}
template<class Memory>
bool eligible(const Memory &memory, Target target) {
    if (!target.name || !memory.contains(target.work, 0x1bb0u) ||
        memory.load32(target.work) != target.name) return false;
    const auto component = memory.load32(target.work + 0x10u);
    if (!memory.contains(component, 0x28u) || memory.load32(component + 4u) != target.name ||
        (memory.load32(component) & 0x11000u) != 0x1000u) return false;
    const auto flags = memory.load32(target.work + 0x1858u);
    const auto kind = memory.load32(target.work + 0x1850u);
    // Same status mask as the original enemy query. No carried actors or
    // scripted prisoners; their callback/state contracts need separate work.
    // +0x1438 is the stage script's own capture procedure, the one a truck
    // delivery runs: 79 in st01, 82 in another stage seen live. Any is taken.
    return (flags & 0x40399u) == 0u && ((flags & 6u) != 0u || held_up(memory, target)) &&
           (memory.load32(target.work + 0x185cu) & 0x02000000u) == 0u &&
           kind >= 20u && kind < 54u && memory.load32(target.work + 0x1438u) != 0u;
}

template<class Memory>
Target nearest(const Memory &memory, std::uint32_t player) {
    Target best;
    float distance = reach * reach;
    unsigned remaining = 4096;
    for (unsigned bucket = 0; bucket < 16 && remaining; ++bucket) {
        auto node = memory.load32(engine + 0x258f8cu + 12u * bucket);
        while (node && remaining) {
            --remaining;
            if (!memory.contains(node, 0x14u)) break;
            const auto component = memory.load32(node + 0x10u);
            if (memory.contains(component, 0x28u)) {
                Target target{memory.load32(component + 0x24u), memory.load32(component + 4u)};
                if (same(memory, target) && memory.load32(target.work + 0x10u) == component) {
                    float squared = 0;
                    std::array<float, 3> to{};
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        to[axis] = std::bit_cast<float>(memory.load32(target.work + 0x30u + axis * 4u)) -
                                   std::bit_cast<float>(memory.load32(player + 0x100u + axis * 4u));
                        squared += to[axis] * to[axis];
                    }
                    const float yaw = float(memory.load32(player + 0x130u) >> 16) * (6.2831853f / 65536.0f);
                    const bool faced = std::sin(yaw) * to[0] + std::cos(yaw) * to[2] >= facing * std::sqrt(squared);
                    if (std::isfinite(squared) && squared < distance && faced) {
                        if (fulton_visuals::lifting(target.work)) {
                        } else if (eligible(memory, target)) { best = target; distance = squared; }
                        else if (memory.load32(target.work + 0x1850u) >= 20u &&
                                 memory.load32(target.work + 0x1850u) < 54u) {
                            // A Triangle press is the only caller during play. Keep
                            // rejected nearby actors identifiable for live RE.
                            std::cout << std::hex << "[fulton] nearby rejected soldier " << target.name
                                      << " work " << target.work << " flags " << memory.load32(target.work + 0x1858u)
                                      << '/' << memory.load32(target.work + 0x185cu)
                                      << " behavior " << memory.load32(target.work + 0x3c0u)
                                      << " action " << memory.load32(target.work + 0x170cu)
                                      << " callback " << memory.load32(target.work + 0x400u)
                                      << std::dec << std::endl;
                        }
                    }
                }
            }
            node = memory.load32(node);
        }
    }
    return best;
}

template<class Memory>
bool appended(const Memory &memory, std::uint32_t system, std::uint32_t before, Target target) {
    if (before >= 80u || memory.load32(system + 0x3c98u) != before + 1u) return false;
    const auto record = system + 0x3ca0u + before * 0x88u;
    return memory.load8(record + 0x70u) == 2u && memory.load32(record + 0x6cu) == target.name &&
           memory.load32(record) != 0u && (memory.load32(record + 0xcu) & 1u) != 0u;
}

inline psprecomp::Runtime::RecompiledFunction original_update = nullptr;
inline psprecomp::Runtime::RecompiledFunction original_stage_init = nullptr;
inline std::uint32_t notify = 0, cleanup = 0;
inline bool held = false, busy = false;
inline std::uint32_t watch_work = 0; inline unsigned watch_frames = 0; // post-capture trace


// The capture script ends by creating the "capture completed" banner: chara
// 0x26E0F6, whose constructor (st01t2 +0xA9C4, live ULUS-10202 1.02) plays
// jingle 0x147 through a stage sound helper and then makes the banner actor,
// at once or 90 frames later for some soldiers. A Fulton capture keeps the
// jingle and drops the banner: for a while after one, the constructor only
// plays the sound and answers as it does when a banner is already up.
inline psprecomp::Runtime::RecompiledFunction original_banner = nullptr;
inline std::uint32_t stage_base = 0, banner = 0, banner_sound = 0;
inline bool banner_searched = false;
inline unsigned quiet_frames = 0;

inline void banner_create(psprecomp::CorpusRuntime &corpus, psprecomp::AllegrexContext &ctx) {
    if (!quiet_frames) { original_banner(corpus, ctx); return; }
    const auto saved = ctx;
    portablekit::hle_extension::call_guest(ctx, banner_sound, {0x147u, 0x2du, 1u, 0u},
        [saved](auto &returned, std::uint32_t) {
            returned = saved;
            returned.gpr[2] = 1;
            returned.gpr[3] = 0;
            returned.pc = returned.gpr[31];
        });
}

// Each stage module carries the chara table, {id, constructor} pairs; found
// once the module is linked, by the id and the constructor's own two constants.
inline void find_banner(psprecomp::Runtime &runtime) {
    if (banner_searched || !stage_base) return;
    banner_searched = true;
    const auto &memory = runtime.memory();
    for (std::uint32_t at = stage_base; memory.contains(at, 8u) && at < stage_base + 0x200000u; at += 4u) {
        if (memory.load32(at) != 0x26e0f6u) continue;
        const auto made = memory.load32(at + 4u);
        if (made < stage_base || !memory.contains(made, 0x60u) || memory.load32(made + 0x14u) != 0x2405002du ||
            memory.load32(made + 0x38u) != 0x24040147u || memory.load32(made + 0x5cu) >> 26u != 3u) continue;
        const auto found = runtime.registered_function(made);
        if (!found || found == &banner_create) return;
        original_banner = found;
        banner = made;
        banner_sound = (made & 0xf0000000u) | (memory.load32(made + 0x5cu) & 0x03ffffffu) << 2u;
        runtime.register_function(made, &banner_create, "po_fulton_banner");
        std::cout << "[fulton] capture banner found" << std::endl;
        return;
    }
}

inline void reset_stage() {
    fulton_visuals::invalidate();
    fulton_sound::stage_changed();
    held = busy = false;
    watch_work = watch_frames = 0;
    quiet_frames = 0;
}

// ULUS-10202 1.02: the stage's first function clears its BSS. Mission retry
// calls it again without loading a module. Drop old heap/actor addresses
// before that reset; disposing them would call the engine on freed resources.
inline void stage_init(psprecomp::CorpusRuntime &runtime, psprecomp::AllegrexContext &ctx) {
    reset_stage();
    original_stage_init(runtime, ctx);
}

inline void module_ready(psprecomp::Runtime &runtime, std::string_view name, std::uint32_t base,
                         std::span<const std::uint8_t> code) {
    if (!original_update) return;
    if (name == "mgp_stage") {
        reset_stage();
        stage_base = base;
        banner = banner_sound = 0;
        banner_searched = false;
        original_stage_init = runtime.registered_function(base);
        if (original_stage_init)
            runtime.register_function(base, &stage_init, "po_fulton_stage_init");
    }
    const auto n = find(code, notify_shape), c = find(code, cleanup_shape);
    if (!n || !c) return;
    notify = base + *n;
    cleanup = base + *c;
    std::cout << "[fulton] stage capture helpers found" << std::endl;
}

// Then next, with the context as it came in.
inline void capture(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx, Target target, bool animated,
                    fulton_visuals::Resume next) {
    auto &memory = runtime.memory();
    const auto system = memory.load32(engine + 0x1f11d0u);
    if (!memory.contains(system, 0x71acu) || !(animated ? same(memory, target) : eligible(memory, target)) ||
        !matches(memory, notify, notify_shape) || !matches(memory, cleanup, cleanup_shape)) {
        busy = false;
        std::cout << "[fulton] target or stage changed; capture cancelled" << std::endl;
        next(ctx);
        return;
    }
    const auto before = memory.load32(system + 0x3c98u);
    if (before >= 80u) {
        busy = false;
        std::cout << "[fulton] pending captures full" << std::endl;
        next(ctx);
        return;
    }
    const auto saved = ctx;
    const auto cleanup_address = cleanup;
    busy = true;
    find_banner(runtime);
    if (banner) quiet_frames = 150;
    // Run the real script callback in this guest thread, with scheduler/HLE
    // support. A single isolated AOT unit cannot execute a whole GCL call.
    portablekit::hle_extension::call_guest(ctx, notify, {target.work},
        [&runtime, saved, target, before, system, cleanup_address, animated, next](auto &returned, std::uint32_t) {
            auto &memory = runtime.memory();
            const auto finish = [saved, next](auto &context) {
                busy = false;
                context = saved;
                next(context);
            };
            if (memory.load32(engine + 0x1f11d0u) != system ||
                !appended(memory, system, before, target)) {
                std::cout << "[fulton] capture rejected; soldier retained" << std::endl;
                finish(returned);
                return;
            }
            if (!matches(memory, cleanup_address, cleanup_shape) ||
                !memory.contains(target.work, 0x1bb0u) || memory.load32(target.work) != target.name) {
                std::cout << "[fulton] capture committed; stage changed before cleanup" << std::endl;
                finish(returned);
                return;
            }
            // Hide only after recruitment commits. Hiding during visual disposal
            // left a retained soldier invisible when the callback rejected him.
            // Match cleanup's flags before it resets the pose to the floor.
            if (animated)
                for (std::uint32_t field : {0x110u, 0x2e0u, 0x2e4u}) {
                    const auto object = memory.load32(target.work + field);
                    if (memory.contains(object + 0x5cu, 4u))
                        memory.store32(object + 0x5cu, memory.load32(object + 0x5cu) | 0x3000u);
                }
            portablekit::hle_extension::call_guest(returned, cleanup_address, {target.work},
                [finish, target](auto &context, std::uint32_t) {
                    std::cout << "[fulton] soldier recovered" << std::endl;
                    watch_work = target.work; watch_frames = 90;
                    finish(context);
                });
        });
}

inline void player_update(psprecomp::CorpusRuntime &corpus, psprecomp::AllegrexContext &ctx) {
    auto &runtime = static_cast<psprecomp::Runtime &>(corpus);
    auto &memory = runtime.memory();
    const auto player = ctx.gpr[4];
    const auto resume = [&runtime](auto &context) { original_update(runtime, context); };
    if (!memory.contains(player, 0xc00u) || memory.load32(engine + 0x2f7060u) != player) { resume(ctx); return; }
    // Every lift in flight has its tick first; the rest runs after them.
    fulton_visuals::tick_all(runtime, ctx, player, [&runtime, player, resume](auto &ctx) {
    auto &memory = runtime.memory();
    if (quiet_frames) --quiet_frames;
    if (watch_frames && --watch_frames % 10 == 0 && memory.contains(watch_work, 0x1bb0u)) {
        const auto component = memory.load32(watch_work + 0x10u);
        std::cout << std::hex << "[fulton] after capture: component " << (memory.contains(component, 4u) ? memory.load32(component) : 0)
                  << " proc " << memory.load32(watch_work + 0x1438u) << " flags " << memory.load32(watch_work + 0x1858u)
                  << " kind " << memory.load32(watch_work + 0x1850u) << " y " << std::bit_cast<float>(memory.load32(watch_work + 0x34u))
                  << std::dec << std::endl;
    }
    const auto pad = memory.load32(player + 0x720u);
    const bool down = memory.contains(pad, 4u) && (memory.load32(pad) & triangle) != 0u;
    const bool pressed = down && !held;
    held = down;
    const auto system = memory.load32(engine + 0x1f11d0u);
    // One attach at a time: the player is free again once his own clip ends,
    // while the soldiers he has attached go on lifting.
    if (!pressed || busy || fulton_visuals::engaged() || !memory.contains(system, 0x71acu) ||
        memory.load32(engine + 0x20f790u) == 0u || memory.load32(player + 0x58u) != 0x7e8110u ||
        (memory.load32(engine + 0x258b74u) & 0x2000u) != 0u ||
        !matches(memory, notify, notify_shape) || !matches(memory, cleanup, cleanup_shape) ||
        !runtime.has_function(notify) || !runtime.has_function(cleanup)) { resume(ctx); return; }
    if (memory.load32(system + 0x3c98u) >= 80u) {
        std::cout << "[fulton] pending captures full" << std::endl;
        resume(ctx);
        return;
    }
    const auto target = nearest(memory, player);
    if (!target.work) { resume(ctx); return; }
    if (fulton_assets::enabled()) {
        fulton_visuals::begin(runtime, ctx, player, target.work, resume,
            [&runtime, target](auto &context, bool complete, fulton_visuals::Resume next) {
                if (complete) capture(runtime, context, target, true, next);
                else {
                    std::cout << "[fulton] visual sequence cancelled; soldier retained" << std::endl;
                    next(context);
                }
            });
    } else capture(runtime, ctx, target, false, resume);
    });
}

inline void install(psprecomp::Runtime &runtime) {
    const char *value = portablekit::hle_extension::env("FULTON");
    // Preserve asset-free instant recovery only as an explicit developer mode.
    const char *visuals = portablekit::hle_extension::env("FULTON_VISUALS");
    const bool instant = value && std::strcmp(value, "1") == 0 &&
                         (!visuals || std::strcmp(visuals, "1") != 0);
    if (!fulton_assets::enabled() && !instant) return;
    original_update = runtime.registered_function(update);
    if (!original_update) return;
    fulton_visuals::install(runtime);
    fulton_sound::install(runtime);
    runtime.register_function(update, &player_update, "po_fulton_player_update");
    std::cout << "[fulton] prototype enabled; Triangle beside a downed soldier" << std::endl;
}
} // namespace mgs::fulton
