#pragma once

#include "camera.hpp"
#include "fulton_assets.hpp"
#include "fulton_sound.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace mgs::fulton_visuals {
constexpr std::uint32_t base = 0x08804000u;
using Context = psprecomp::AllegrexContext;
using Runtime = psprecomp::Runtime;
using Resume = std::function<void(Context &)>;
using Finish = std::function<void(Context &, bool)>;

// The attach plays PO's own crouch-and-place clip (TNT, claymore, magazine):
// archive sna_wp_claymore (GV_StrCode 0xCCBFF6), clip 0x25, full-body bone
// mask 0x7FFFF, as the game requests it (traced, PO ULUS-10202 1.02).
constexpr std::uint32_t kPlaceArchive = 0xccbff6u, kPlaceClip = 0x25u;
// The soldier plays PW's own Fulton sequence, enedam clips 101 (yank), 103
// (settle) and 13 (hang, looping), traced in PW MC build 0x6AA628A0 and
// converted by tools/portable-ops/mtar.py into fult_enemy.mtar as clips 0-2.
constexpr std::uint32_t kLiftArchive = 0xc11feau;
constexpr std::uint32_t kYankClip = 0, kSettleClip = 1; // clip 2 (hang loop) unused for now

enum class Phase { Idle, Preparing, Attaching, Yank, Settle };

// A motion block (actor field -> {archive, channel}) switched to another archive.
// Its channel holds two 0x80-byte layers (full body +0x00, upper body +0x80),
// each with the archive at +0x04 and its name hash at +0x48. Only those five
// words change hands; restoring a whole older channel snapshot over state the
// game has moved on from (a soldier woken by an alarm) crashed playback.
struct Override {
    std::uint32_t motion{}, channel{};
    std::array<std::uint32_t, 5> at{}, old{}, mine{};
    bool active = false;

    template<class Memory> bool capture(const Memory &memory, std::uint32_t block) {
        motion = block;
        if (!memory.contains(motion, 0xc0)) return false;
        channel = memory.load32(motion + 4u);
        at = {motion, channel + 4u, channel + 0x48u, channel + 0x84u, channel + 0xc8u};
        return memory.contains(channel, 0x100);
    }
    template<class Memory> void apply(Memory &memory, std::uint32_t archive, std::uint32_t hash) {
        mine = {archive, archive, hash, archive, hash};
        for (unsigned i = 0; i < at.size(); ++i) {
            old[i] = memory.load32(at[i]);
            memory.store32(at[i], mine[i]);
        }
        active = true;
    }
    // Hands back only words still holding ours; the game's own changes stand.
    template<class Memory> void restore(Memory &memory) {
        if (!active || !memory.contains(motion, 0xc0) || !memory.contains(channel, 0x100)) return;
        for (unsigned i = 0; i < at.size(); ++i)
            if (memory.load32(at[i]) == mine[i]) memory.store32(at[i], old[i]);
        active = false;
    }
    template<class Memory> bool ended(const Memory &memory) const {
        return (memory.load32(channel + 0x20u) & 5u) == 4u;
    }
};

struct State {
    Phase phase = Phase::Idle;
    std::uint32_t player{}, enemy{}, object{}, harness{}, place{}, lift{};
    Override snake, soldier;
    std::array<float, 3> position{};
    unsigned frames{}, phase_frames{};
    bool sounded = false;
    Finish finish;
};
inline State state;
inline psprecomp::Runtime::RecompiledFunction original_motion = nullptr;
inline bool selecting_motion = false;

// The soldier's motion block is work +0x118 (PO ULUS-10202 1.02). A downed
// soldier plays enedam (0x141D5E) there, not enemy (0x88A21F).
template<class Memory>
std::uint32_t soldier_motion(const Memory &memory, std::uint32_t work) {
    const auto motion = memory.load32(work + 0x118u);
    if (!memory.contains(motion, 0xc0)) return 0;
    const auto channel = memory.load32(motion + 4u);
    return memory.contains(channel, 0x100) && memory.load32(channel + 4u) == memory.load32(motion) ? motion : 0;
}

inline bool trace_motion = false;
// The engine's latest own request (a1..t1) per motion and layer. A layer clip
// we put on plays until the engine asks again, and it only asks on a state
// change, so the hand-back replays these (modern-controls-mgs3's "learned
// halves", docs/animation-system.md).
using Request = std::array<std::uint32_t, 5>;
inline std::unordered_map<std::uint32_t, std::array<std::optional<Request>, 2>> learned;
// Heights above the spine bone; MGSPO_FULTON_BALLOON_Y / _HARNESS_Y override.
inline float balloon_y = 600, harness_y = 300;

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
    // While an override plays, the game's own requests for that motion wait;
    // the last one per layer is kept for the hand-back.
    Override *held = state.snake.active && ctx.gpr[4] == state.snake.motion       ? &state.snake
                     : state.soldier.active && ctx.gpr[4] == state.soldier.motion ? &state.soldier
                                                                                  : nullptr;
    if (!selecting_motion)
        learned[ctx.gpr[4]][ctx.gpr[5] & 1u] = Request{ctx.gpr[5], ctx.gpr[6], ctx.gpr[7], ctx.gpr[8], ctx.gpr[9]};
    if (!selecting_motion && held) {
        ctx.gpr[2] = 0;
        ctx.pc = ctx.gpr[31];
        return;
    }
    original_motion(runtime, ctx);
}

inline void invalidate() {
    if (state.phase != Phase::Idle)
        std::cout << "[fulton] visual sequence cancelled by stage change" << std::endl;
    // Stage resources and tracked render allocations belong to the game.
    // Their old addresses must not be dereferenced after a module transition.
    state = {};
    selecting_motion = false;
    learned.clear();
}

template<class Memory>
void hand_back(Memory &memory, Context &ctx, Override &ov, std::function<void(Context &)> next);

inline void dispose(Runtime &runtime, Context &ctx, bool complete) {
    const auto saved = ctx;
    auto finish = [saved, complete](Context &context) {
        auto callback = std::move(state.finish);
        state = {};
        selecting_motion = false;
        context = saved;
        if (callback) callback(context, complete);
    };
    auto &memory = runtime.memory();
    if (complete) state.soldier.restore(memory); // he is removed; no requests to replay
    // Hide the soldier as the capture cleanup (stage 0x6D2AC) will: body at work
    // +0x110, attachments at +0x2E0/+0x2E4, flags 0x3000 at object +0x5C. Done
    // first, the pose the cleanup resets him to is never drawn.
    if (complete && memory.contains(state.enemy, 0x2e8u))
        for (std::uint32_t field : {0x110u, 0x2e0u, 0x2e4u}) {
            const auto object = memory.load32(state.enemy + field);
            if (memory.contains(object + 0x5cu, 4u)) memory.store32(object + 0x5cu, memory.load32(object + 0x5cu) | 0x3000u);
        }
    // Unregister and free each render object we made (balloon, harness).
    auto remove = std::make_shared<std::function<void(Context &, std::vector<std::uint32_t>)>>();
    *remove = [finish, remove](Context &context, std::vector<std::uint32_t> objects) {
        if (objects.empty()) { finish(context); return; }
        const auto object = objects.back();
        objects.pop_back();
        portablekit::hle_extension::call_guest(context, base + 0x47394u, {object},
            [object, objects, remove](auto &removed, std::uint32_t) {
                portablekit::hle_extension::call_guest(removed, base + 0x47418u, {object},
                    [objects, remove](auto &returned, std::uint32_t) { (*remove)(returned, objects); });
            });
    };
    std::vector<std::uint32_t> objects;
    for (auto object : {state.object, state.harness}) if (object) objects.push_back(object);
    hand_back(memory, ctx, state.snake, [&memory, remove, objects](Context &context) {
        hand_back(memory, context, state.soldier, [remove, objects](Context &c) { (*remove)(c, objects); });
    });
}

// Request clip on a motion as the game does (layer 0, full-body mask), then next.
inline void play(Context &ctx, std::uint32_t motion, std::uint32_t clip, std::function<void(Context &)> next) {
    selecting_motion = true;
    portablekit::hle_extension::call_guest(ctx, base + 0x7f264u, {motion, 0, clip, 0, 0x7ffffu, 0},
        [next](auto &context, std::uint32_t) { selecting_motion = false; next(context); });
}

// Hands a motion back: the archive words, then the engine's last own request
// for each layer (learned), so it plays what it thinks is playing. A layer it
// never stated is released with the engine's layer reset (0x7F634: clip -1,
// cursor cleared) rather than given a clip of ours, which would loop.
template<class Memory>
void hand_back(Memory &memory, Context &ctx, Override &ov, std::function<void(Context &)> next) {
    const bool was = ov.active;
    ov.restore(memory);
    std::vector<std::pair<std::uint32_t, std::vector<std::uint32_t>>> calls;
    if (was) {
        const auto &known = learned[ov.motion];
        for (std::uint32_t layer = 0; layer < 2; ++layer) {
            if (const auto &r = known[layer])
                calls.push_back({0x7f264u, {ov.motion, (*r)[0], (*r)[1], (*r)[2], (*r)[3], (*r)[4]}});
            else if (layer == 1)
                calls.push_back({0x7f634u, {ov.motion, 1u}});
            if (trace_motion)
                std::cout << std::hex << "[fulton] hand-back " << ov.motion << " layer " << layer << " clip "
                          << (known[layer] ? (*known[layer])[1] : 0xffffffffu) << std::dec << std::endl;
        }
    }
    auto step = std::make_shared<std::function<void(Context &, std::size_t)>>();
    *step = [calls, next, step](Context &context, std::size_t i) {
        if (i == calls.size()) { selecting_motion = false; next(context); return; }
        selecting_motion = true;
        const auto &[function, args] = calls[i];
        portablekit::hle_extension::call_guest(context, base + function,
            std::span<const std::uint32_t>(args), [step, i](auto &returned, std::uint32_t) { (*step)(returned, i + 1); });
    };
    (*step)(ctx, 0);
}

// Looks up a stage resource by type and name hash (0x08 motion archive, 0x13 model).
template<class Then>
void lookup(Context &ctx, std::uint32_t key, Then then) {
    portablekit::hle_extension::call_guest(ctx, base + 0x3bb18u, {key}, then);
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
    const auto soldier = soldier_motion(memory, enemy);
    if (!original_motion || !state.snake.capture(memory, memory.load32(player + 0x1e8u)) ||
        !state.soldier.capture(memory, soldier)) {
        std::cerr << "[fulton] player or soldier motion not found" << std::endl;
        dispose(runtime, ctx, false);
        return;
    }
    for (unsigned i = 0; i < 3; ++i)
        state.position[i] = std::bit_cast<float>(memory.load32(enemy + 0x30u + i * 4u));
    auto fail = [&runtime, saved](Context &context, const char *why) {
        std::cerr << "[fulton] " << why << std::endl;
        context = saved;
        dispose(runtime, context, false);
    };
    // The sound bank first: a stage load resets the heap it lives in.
    fulton_sound::prepare(runtime, ctx, [&runtime, saved, resume, fail](Context &ctx) {
    lookup(ctx, 0x08000000u | kPlaceArchive, [&runtime, saved, resume, fail](auto &context, std::uint32_t place) {
        if (runtime.memory().load32(place) != 0x7261744du) return fail(context, "placement motion (sna_wp_claymore) not loaded");
        state.place = place;
        lookup(context, 0x08000000u | kLiftArchive, [&runtime, saved, resume, fail](auto &context, std::uint32_t lift) {
            if (runtime.memory().load32(lift) != 0x7261744du) return fail(context, "soldier lift motion (fult_enemy) not loaded");
            state.lift = lift;
            lookup(context, 0x13f66bb7u, [&runtime, saved, resume, fail](auto &context, std::uint32_t model) {
                if (!runtime.memory().contains(model, 0x50) || runtime.memory().load32(model) != 0x2050444du)
                    return fail(context, "balloon model unavailable");
                portablekit::hle_extension::call_guest(context, base + 0x46cf8u, {model, 0x12u, 0},
                    [&runtime, saved, resume, fail](auto &created, std::uint32_t object) {
                        state.object = object;
                        if (!object) return fail(created, "balloon object not created");
                        portablekit::hle_extension::call_guest(created, base + 0x47378u, {object},
                            [&runtime, saved, resume, fail](auto &registered, std::uint32_t) {
                    lookup(registered, 0x13430af6u, [&runtime, saved, resume, fail](auto &context, std::uint32_t model) {
                        if (!runtime.memory().contains(model, 0x50) || runtime.memory().load32(model) != 0x2050444du)
                            return fail(context, "harness model (fult_item_body) unavailable");
                        portablekit::hle_extension::call_guest(context, base + 0x46cf8u, {model, 0x12u, 0},
                            [&runtime, saved, resume, fail](auto &created, std::uint32_t harness) {
                                state.harness = harness;
                                if (!harness) return fail(created, "harness object not created");
                                portablekit::hle_extension::call_guest(created, base + 0x47378u, {harness},
                                    [&runtime, saved, resume](auto &registered, std::uint32_t) {
                                state.snake.apply(runtime.memory(), state.place, kPlaceArchive);
                                play(registered, state.snake.motion, kPlaceClip, [saved, resume](Context &playing) {
                                    state.phase = Phase::Attaching;
                                    std::cout << "[fulton] player attachment and balloon started" << std::endl;
                                    playing = saved;
                                    resume(playing);
                                });
                                    });
                            });
                    });
                            });
                    });
            });
        });
    });
    });
}

inline bool tick(Runtime &runtime, Context &ctx, std::uint32_t player, Resume resume) {
    if (state.phase == Phase::Idle || state.phase == Phase::Preparing) return false;
    auto &memory = runtime.memory();
    if (state.player != player || !memory.contains(state.object, 0x100u) || !memory.contains(state.harness, 0x100u) ||
        !memory.contains(state.snake.channel, 0x100) || !memory.contains(state.soldier.channel, 0x100) ||
        !memory.contains(state.enemy + 0x30u, 12u)) { dispose(runtime, ctx, false); return true; }
    if (++state.frames > 900) {
        std::cerr << "[fulton] sequence timed out; capture cancelled" << std::endl;
        dispose(runtime, ctx, false);
        return true;
    }
    ++state.phase_frames;
    if (trace_motion) {
        std::cout << std::hex << "[fulton] tick phase " << int(state.phase) << " snake";
        for (unsigned i = 0; i < 0x20; i += 4) std::cout << ' ' << memory.load32(state.snake.motion + i);
        std::cout << " | soldier";
        for (unsigned i = 0; i < 0x20; i += 4) std::cout << ' ' << memory.load32(state.soldier.motion + i);
        std::cout << std::dec << std::endl;
    }
    std::optional<std::uint32_t> next_clip;
    bool hand_snake = false, sound = false;
    float scale = 1, harness_scale = 1, rise = 0;
    switch (state.phase) {
    case Phase::Attaching: {
        mgs::set_action(runtime, ctx, player, 0x13bu);
        const float frame = std::bit_cast<float>(memory.load32(state.snake.channel + 0x38u));
        const float end = std::bit_cast<float>(memory.load32(state.snake.channel + 0x30u));
        const float progress = end > 0 ? std::clamp(frame / end, 0.0f, 1.0f) : 0;
        scale = std::clamp((progress - 0.55f) * 3, 0.02f, 1.0f);
        harness_scale = progress > 0.4f ? 1.0f : 0.02f; // clipped on mid-placement
        // PW's Fulton sound (inflate, then the lift) starts as the balloon does.
        if (!state.sounded && progress > 0.55f) sound = state.sounded = true;
        if (state.snake.ended(memory) && state.phase_frames > 2) {
            hand_snake = true;
            state.soldier.apply(memory, state.lift, kLiftArchive);
            state.phase = Phase::Yank;
            state.phase_frames = 0;
            next_clip = kYankClip;
        }
        break;
    }
    case Phase::Yank:
        if (state.soldier.ended(memory) && state.phase_frames > 2) {
            next_clip = kSettleClip;
            state.phase = Phase::Settle;
            state.phase_frames = 0;
        }
        break;
    case Phase::Settle: {
        // Capture on the settle clip's last frame: once it ends, his own
        // routine takes the pose back (asleep on the floor) before cleanup.
        const float frame = std::bit_cast<float>(memory.load32(state.soldier.channel + 0x38u));
        const float end = std::bit_cast<float>(memory.load32(state.soldier.channel + 0x30u));
        if (state.phase_frames > 2 && (state.soldier.ended(memory) || (end > 0 && frame >= end - 1))) {
            dispose(runtime, ctx, true);
            return true;
        }
        const float progress = end > 0 ? std::clamp(frame / end, 0.0f, 1.0f) : 0;
        rise = progress * progress * 3000;
        break;
    }
    default: break;
    }
    // The balloon rides the soldier's upper spine (bone 2): render object at
    // work +0x110, bone matrices at object +0xAC, 0x40 each, translation +0x30.
    auto anchor = state.position;
    float above = 800;
    const auto body = memory.load32(state.enemy + 0x110u);
    if (memory.contains(body + 0xacu, 4u)) {
        const auto spine = memory.load32(body + 0xacu) + 2u * 0x40u + 0x30u;
        if (memory.contains(spine, 12u)) {
            for (unsigned i = 0; i < 3; ++i) anchor[i] = std::bit_cast<float>(memory.load32(spine + i * 4u));
            above = balloon_y;
        }
    }
    // Balloon above the spine; harness and tie on it.
    auto place = [&memory, &anchor](std::uint32_t object, float scale, float height) {
        for (unsigned i = 0; i < 16; ++i) {
            float value = i == 15 ? 1.0f : (i == 0 || i == 5 || i == 10 ? scale : 0.0f);
            if (i >= 12 && i <= 14) value = anchor[i - 12] + (i == 13 ? height : 0);
            memory.store32(object + 0x10u + i * 4u, std::bit_cast<std::uint32_t>(value));
        }
    };
    place(state.object, scale, above + rise);
    place(state.harness, harness_scale, harness_y);
    const auto saved = ctx;
    auto draw = [saved, resume](Context &context) {
        portablekit::hle_extension::call_guest(context, base + 0x14b00u, {state.object},
            [saved, resume](auto &balloon, std::uint32_t) {
                portablekit::hle_extension::call_guest(balloon, base + 0x14b00u, {state.harness},
                    [saved, resume](auto &drawn, std::uint32_t) { drawn = saved; resume(drawn); });
            });
    };
    if (next_clip) std::cout << std::hex << "[fulton] soldier clip " << *next_clip << " motion " << state.soldier.motion
                              << " archive " << state.lift << std::dec << std::endl;
    auto soldier = [next_clip, draw](Context &context) {
        if (next_clip) play(context, state.soldier.motion, *next_clip, draw);
        else draw(context);
    };
    auto snake = [&memory, hand_snake, soldier](Context &context) {
        if (hand_snake) hand_back(memory, context, state.snake, soldier);
        else soldier(context);
    };
    if (sound) fulton_sound::play(ctx, state.enemy + 0x30u, snake);
    else snake(ctx);
    return true;
}

inline void install(Runtime &runtime) {
    if (!fulton_assets::enabled()) return;
    trace_motion = portablekit::hle_extension::env("FULTON_TRACE_MOTION") != nullptr;
    if (const char *y = portablekit::hle_extension::env("FULTON_BALLOON_Y")) balloon_y = std::strtof(y, nullptr);
    if (const char *y = portablekit::hle_extension::env("FULTON_HARNESS_Y")) harness_y = std::strtof(y, nullptr);
    original_motion = runtime.registered_function(base + 0x7f264u);
    if (original_motion) runtime.register_function(base + 0x7f264u, &motion_select, "po_fulton_motion_select");
}
} // namespace mgs::fulton_visuals
