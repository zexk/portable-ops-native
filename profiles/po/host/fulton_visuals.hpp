#pragma once

#include "camera.hpp"
#include "fulton_assets.hpp"

#include <functional>

namespace mgs::fulton_visuals {
constexpr std::uint32_t base = 0x08804000u;
using Context = psprecomp::AllegrexContext;
using Runtime = psprecomp::Runtime;
using Resume = std::function<void(Context &)>;
using Finish = std::function<void(Context &, bool)>;
enum class Phase { Idle, Preparing, Attaching, Rising };
struct State {
    Phase phase = Phase::Idle;
    std::uint32_t player{}, enemy{}, object{}, motion{}, channel{}, archive{}, old_archive{};
    std::array<std::uint32_t, 64> old_channel{}; // two 0x80-byte layers
    std::array<float, 3> position{};
    unsigned frames{}, rise_frames{};
    bool motion_overridden = false;
    Finish finish;
};
inline State state;
// The attach plays PO's own crouch-and-place clip (TNT, claymore, magazine):
// archive sna_wp_claymore (GV_StrCode 0xCCBFF6), clip 0x25, full-body bone
// mask 0x7FFFF, as the game requests it (traced, PO ULUS-10202 1.02).
constexpr std::uint32_t kPlaceArchive = 0xccbff6u, kPlaceClip = 0x25u;
inline psprecomp::Runtime::RecompiledFunction original_motion = nullptr;
inline bool selecting_motion = false;

inline bool trace_motion = false;

inline void motion_select(psprecomp::CorpusRuntime &runtime, Context &ctx) {
    if (trace_motion) {
        auto &memory = static_cast<Runtime &>(runtime).memory();
        const auto motion = ctx.gpr[4];
        const auto channel = memory.contains(motion, 8) ? memory.load32(motion + 4u) : 0;
        std::cout << std::hex << "[fulton] motion a0=" << motion << " a1=" << ctx.gpr[5] << " a2=" << ctx.gpr[6]
                  << " a3=" << ctx.gpr[7] << " t0=" << ctx.gpr[8] << " t1=" << ctx.gpr[9] << " ra=" << ctx.gpr[31]
                  << " archive=" << (memory.contains(motion, 4) ? memory.load32(motion) : 0)
                  << " clip=" << (memory.contains(channel, 0x4c) ? memory.load32(channel + 0x48u) : 0)
                  << std::dec << std::endl;
    }
    if (state.phase == Phase::Attaching && ctx.gpr[4] == state.motion && !selecting_motion) {
        ctx.gpr[2] = 0;
        ctx.pc = ctx.gpr[31];
        return;
    }
    const auto motion = ctx.gpr[4];
    original_motion(runtime, ctx);
    if (trace_motion) {
        // Player only (snake or sna_wp_claymore): motion and channel words after the call.
        auto &memory = static_cast<Runtime &>(runtime).memory();
        const auto channel = memory.contains(motion, 8) ? memory.load32(motion + 4u) : 0;
        const auto clip = memory.contains(channel, 0x4c) ? memory.load32(channel + 0x48u) : 0;
        if ((clip == 0x6891ccu || clip == kPlaceArchive) && memory.contains(motion, 0xc0) &&
            memory.contains(channel, 0x100)) {
            std::cout << std::hex << "[fulton] motion-words";
            for (unsigned i = 0; i < 0xc0; i += 4) std::cout << ' ' << memory.load32(motion + i);
            std::cout << "\n[fulton] channel-words";
            for (unsigned i = 0; i < 0x100; i += 4) std::cout << ' ' << memory.load32(channel + i);
            std::cout << std::dec << std::endl;
        }
    }
}

inline void restore_motion(Runtime &runtime) {
    auto &memory = runtime.memory();
    if (!state.motion_overridden || !state.motion || !memory.contains(state.player, 0x1ecu) ||
        memory.load32(state.player + 0x1e8u) != state.motion ||
        !memory.contains(state.channel, 0x100)) return;
    memory.store32(state.motion, state.old_archive);
    for (unsigned i = 0; i < state.old_channel.size(); ++i)
        memory.store32(state.channel + i * 4u, state.old_channel[i]);
    state.motion_overridden = false;
}

inline void invalidate() {
    if (state.phase != Phase::Idle)
        std::cout << "[fulton] visual sequence cancelled by stage change" << std::endl;
    // Stage resources and tracked render allocations belong to the game.
    // Their old addresses must not be dereferenced after a module transition.
    state = {};
    selecting_motion = false;
}

inline void dispose(Runtime &runtime, Context &ctx, bool complete) {
    const auto saved = ctx;
    auto finish = [saved, complete](Context &context) {
        auto callback = std::move(state.finish);
        state = {};
        selecting_motion = false;
        context = saved;
        if (callback) callback(context, complete);
    };
    restore_motion(runtime);
    if (!state.object) { finish(ctx); return; }
    const auto object = state.object;
    portablekit::hle_extension::call_guest(ctx, base + 0x47394u, {object},
        [&runtime, object, finish](auto &context, std::uint32_t) {
            portablekit::hle_extension::call_guest(context, base + 0x47418u, {object},
                [finish](auto &returned, std::uint32_t) { finish(returned); });
        });
}

inline void begin(Runtime &runtime, Context &ctx, std::uint32_t player, std::uint32_t enemy,
                  Resume resume, Finish finish) {
    state = {};
    state.phase = Phase::Preparing;
    state.player = player;
    state.enemy = enemy;
    state.finish = std::move(finish);
    const auto saved = ctx;
    auto &memory = runtime.memory();
    state.motion = memory.load32(player + 0x1e8u);
    if (!original_motion || !memory.contains(state.motion, 0xc0)) {
        dispose(runtime, ctx, false);
        return;
    }
    state.channel = memory.load32(state.motion + 4u);
    if (!memory.contains(state.channel, 0x100)) { dispose(runtime, ctx, false); return; }
    state.old_archive = memory.load32(state.motion);
    for (unsigned i = 0; i < state.old_channel.size(); ++i) state.old_channel[i] = memory.load32(state.channel + i * 4u);
    for (unsigned i = 0; i < 3; ++i)
        state.position[i] = std::bit_cast<float>(memory.load32(enemy + 0x30u + i * 4u));
            portablekit::hle_extension::call_guest(ctx, base + 0x3bb18u, {0x08000000u | kPlaceArchive},
                [&runtime, saved, resume](auto &context, std::uint32_t archive) {
                    auto &memory = runtime.memory();
                    if (!memory.contains(archive, 32u) || memory.load32(archive) != 0x7261744du) {
                        std::cerr << "[fulton] placement motion (sna_wp_claymore) not loaded" << std::endl;
                        context = saved;
                        dispose(runtime, context, false);
                        return;
                    }
                    state.archive = archive;
                    portablekit::hle_extension::call_guest(context, base + 0x3bb18u, {0x13f66bb7u},
                        [&runtime, saved, resume](auto &found, std::uint32_t model) {
                            if (!runtime.memory().contains(model, 0x50) ||
                                runtime.memory().load32(model) != 0x2050444du) {
                                std::cerr << "[fulton] balloon model unavailable" << std::endl;
                                found = saved;
                                dispose(runtime, found, false);
                                return;
                            }
                            portablekit::hle_extension::call_guest(found, base + 0x46cf8u, {model, 0x12u, 0},
                                [&runtime, saved, resume](auto &created, std::uint32_t object) {
                                    state.object = object;
                                    if (!object) { created = saved; dispose(runtime, created, false); return; }
                                    portablekit::hle_extension::call_guest(created, base + 0x47378u, {object},
                                        [&runtime, saved, resume](auto &registered, std::uint32_t) {
                                            auto &memory = runtime.memory();
                                            memory.store32(state.motion, state.archive);
                                            // Both layers (full body +0x00, upper body +0x80) take the archive, as the game does.
                                            for (std::uint32_t layer : {0u, 0x80u}) {
                                                memory.store32(state.channel + layer + 4u, state.archive);
                                                memory.store32(state.channel + layer + 0x48u, kPlaceArchive);
                                            }
                                            state.motion_overridden = true;
                                            selecting_motion = true;
                                            portablekit::hle_extension::call_guest(registered, base + 0x7f264u,
                                                {state.motion, 0, kPlaceClip, 0, 0x7ffffu, 0},
                                                [saved, resume](auto &playing, std::uint32_t) {
                                                    selecting_motion = false;
                                                    state.phase = Phase::Attaching;
                                                    std::cout << "[fulton] player attachment and balloon started" << std::endl;
                                                    playing = saved;
                                                    resume(playing);
                                                });
                                        });
                                });
                        });
                });
}

inline bool tick(Runtime &runtime, Context &ctx, std::uint32_t player, Resume resume) {
    if (state.phase == Phase::Idle || state.phase == Phase::Preparing) return false;
    auto &memory = runtime.memory();
    if (state.player != player || !memory.contains(state.object, 0x100u) ||
        !memory.contains(state.channel, 0x100)) { dispose(runtime, ctx, false); return true; }
    if (++state.frames > 400) {
        std::cerr << "[fulton] attachment timed out; capture cancelled" << std::endl;
        dispose(runtime, ctx, false);
        return true;
    }
    float progress = 1;
    if (state.phase == Phase::Attaching) {
        mgs::set_action(runtime, ctx, player, 0x13bu);
        const float frame = std::bit_cast<float>(memory.load32(state.channel + 0x38u));
        const float end = std::bit_cast<float>(memory.load32(state.channel + 0x30u));
        progress = end > 0 ? std::clamp(frame / end, 0.0f, 1.0f) : 0;
        if ((memory.load32(state.channel + 0x20u) & 5u) == 4u && state.frames > 2) {
            restore_motion(runtime);
            state.phase = Phase::Rising;
        }
    } else if (++state.rise_frames >= 30) {
        dispose(runtime, ctx, true);
        return true;
    }
    const float scale = state.phase == Phase::Rising ? 1 : std::clamp((progress - 0.55f) * 3, 0.02f, 1.0f);
    const float rise = state.rise_frames / 30.0f;
    for (unsigned i = 0; i < 16; ++i) {
        float value = i == 15 ? 1.0f : (i == 0 || i == 5 || i == 10 ? scale : 0.0f);
        if (i >= 12 && i <= 14) value = state.position[i - 12] + (i == 13 ? 800 + rise * rise * 5000 : 0);
        memory.store32(state.object + 0x10u + i * 4u, std::bit_cast<std::uint32_t>(value));
    }
    const auto saved = ctx;
    portablekit::hle_extension::call_guest(ctx, base + 0x14b00u, {state.object},
        [saved, resume](auto &context, std::uint32_t) { context = saved; resume(context); });
    return true;
}

inline void install(Runtime &runtime) {
    if (!fulton_assets::enabled()) return;
    trace_motion = portablekit::hle_extension::env("FULTON_TRACE_MOTION") != nullptr;
    original_motion = runtime.registered_function(base + 0x7f264u);
    if (original_motion) runtime.register_function(base + 0x7f264u, &motion_select, "po_fulton_motion_select");
}
} // namespace mgs::fulton_visuals
