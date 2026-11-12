#pragma once

// Experimental crouched locomotion, PO ULUS-10202 1.02. The native crouch
// decision turns movement into a prone transition. Keep that decision's
// other checks, but let PW's two loops supply the movement displacement.
#include "fulton_convert.hpp"
#include "fulton_visuals.hpp"
#include "guest_wrap.hpp"
#include "install/user_data.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>

namespace mgs::crouchwalking {
using Bytes = std::vector<std::uint8_t>;
using Context = psprecomp::AllegrexContext;
using Runtime = psprecomp::Runtime;
using Resume = std::function<void(Context &)>;
constexpr std::uint32_t engine = 0x08804000u, decision = engine + 0x10174cu;
constexpr std::uint32_t idle = engine + 0x10174cu, locomotion = engine + 0x105564u;
constexpr std::uint32_t archive_hash = 0x700e98u;
constexpr std::uint32_t blend_ticks = 35u; // PO's default full-body transition (state +0x28).
// With the Mk22 out, PO's upper layer (channel +0x80) plays sna_wp_mk22, whose
// crouch idle arms look wrong over walking legs. PW's Mk.22 (level 1, SLOT
// key below) has an upper body for each gait, clips 8 and 6, as long as the
// loops; they become converted clips 2 and 3, on layer 1 with PO's upper mask.
constexpr std::uint32_t mk22_po = 0x4c4af2u, mk22_pw = 0x08dc2535u, upper_mask = 0x1ffeu;
inline bool selected = false, available = false, enabled = false, has_upper = false;
inline bool upper_imported = false;  // the installed archive has the Mk.22's clips
inline std::string status;
inline Bytes bytes;
inline std::uint32_t resource = 0, owner = 0, state = 0, motion = 0, channel = 0;
inline std::array<std::uint32_t, 3> saved{};
inline std::array<std::uint32_t, 2> saved_upper{};  // layer 1's archive and its hash
inline int old_clip = 0, old_upper = -1, playing = -1;
inline bool attempted = false, inner = false, upper_ours = false;
inline Runtime::RecompiledFunction original = nullptr, original_stage = nullptr;
inline Runtime::RecompiledFunction original_request = nullptr, original_release = nullptr;
inline GuestWrap movement;

inline std::filesystem::path directory() {
    return portablekit::install::user_data_directory() / "pw" / "crouchwalking";
}

// The converted clips: the normal and slow gaits, then, from an import that
// found them, the Mk.22's upper body for each.
struct Expected { std::uint32_t name, kind, frames; };
constexpr Expected expected[4] = {
    {0x1c60c2u, 0x436u, 40}, {0x4c0730u, 0x436u, 112}, {0x29b93du, 0x10436u, 40}, {0x7db0acu, 0x10436u, 112}};

inline bool valid(std::span<const std::uint8_t> data) {
    try {
        using fulton_convert::get;
        if (data.size() > 65536 || get(data, 0) != 0x7261744du || get(data, 4) != 19 ||
            (get(data, 8) != 2 && get(data, 8) != 4) || get(data, 16) != 0x10000) return false;
        const std::uint32_t count = get(data, 8), body = get(data, 12);
        if (body < 184 + count * 8 || body > data.size()) return false;
        for (unsigned joint = 0; joint < 19; ++joint) {
            const auto parent = fulton_convert::joint_parents[joint];
            if (get(data, 32 + joint * 4) != fulton_convert::joint_names[joint] ||
                get(data, 108 + joint * 4) != (parent == 255 ? 0u : fulton_convert::joint_names[parent])) return false;
        }
        for (unsigned i = 0; i < count; ++i) {
            const std::uint64_t at = std::uint64_t(body) + get(data, 184 + i * 8);
            const std::uint32_t size = get(data, 188 + i * 8);
            if (at + size > data.size() || size < 140 ||
                get(data, at) != expected[i].name || get(data, at + 4) != expected[i].kind ||
                get(data, at + 8) != 5 || get(data, at + 12) != expected[i].frames ||
                get(data, at + 24) != 19 || get(data, at + 16) != 140 ||
                140ull + get(data, at + 20) * 2ull > size) return false;
            for (unsigned joint = 0; joint < 19; ++joint)
                if (get(data, at + 64 + joint * 4) > get(data, at + 20)) return false;
            for (unsigned tail = 0; tail < 4; ++tail)
                if (get(data, at + 0x30 + tail * 4) > get(data, at + 20)) return false;
        }
        return true;
    } catch (const std::exception &) { return false; }
}

inline Bytes read(const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    Bytes data(65537);
    input.read(reinterpret_cast<char *>(data.data()), data.size());
    data.resize(std::size_t(input.gcount()));
    if (!valid(data)) throw std::runtime_error("Missing or invalid crouchwalk archive");
    return data;
}

inline void refresh() {
    try {
        upper_imported = fulton_convert::get(read(directory() / "crouchwalk.mtar"), 8) == 4;
        available = true;
        status = "Assets installed";
    } catch (const std::exception &) {
        available = upper_imported = false;
        status = "Converted crouchwalk assets required";
    }
}

inline void save(bool value) {
    try {
        auto entries = portablekit::install::read_settings_file(portablekit::install::user_data_directory());
        entries["pw.crouchwalking"] = value ? "1" : "0";
        portablekit::install::write_settings_file(portablekit::install::user_data_directory(), entries);
        selected = value;
    } catch (const std::exception &error) { status = error.what(); }
}

inline std::string import(const std::filesystem::path &chosen) {
    try {
        Bytes converted;
        const auto extract = [&converted](const pw_slot::Source &source) {
            Bytes coop;
            pw_slot::stage_files(source, [&](std::string_view name, std::span<const std::uint8_t> data) {
                if (name != "snake_coop.mtar" || !converted.empty()) return !converted.empty();
                try {
                    auto candidate = fulton_convert::mtar(data, {7, 6}, source.wide);
                    if (valid(candidate)) converted = std::move(candidate), coop.assign(data.begin(), data.end());
                } catch (const std::runtime_error &) {}
                return !converted.empty();
            });
            if (converted.empty()) throw std::runtime_error("PW source lacks supported crouchwalk loops");
            // The Mk.22's upper body for both gaits, when the source has it.
            try {
                const auto found = pw_slot::extract(source, std::array{mk22_pw});
                if (const auto mk22 = found.find(mk22_pw); mk22 != found.end()) {
                    const fulton_convert::Clip clips[] = {{coop, 7}, {coop, 6}, {mk22->second, 8}, {mk22->second, 6}};
                    auto with = fulton_convert::mtar(clips, source.wide);
                    if (valid(with)) converted = std::move(with);
                }
            } catch (const std::runtime_error &) {}
        };
        if (std::filesystem::is_directory(chosen)) {
            if (std::filesystem::is_regular_file(chosen / "crouchwalk.mtar"))
                converted = read(chosen / "crouchwalk.mtar");
            else extract(pw_slot::master_collection(chosen));
        } else {
            portablekit::IsoImage iso(chosen);
            extract(pw_slot::psp(iso));
        }
        std::filesystem::create_directories(directory());
        const auto target = directory() / "crouchwalk.mtar", temporary = directory() / "crouchwalk.mtar.tmp";
        const auto backup = directory() / "crouchwalk.mtar.backup";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(converted.data()), converted.size());
        if (!output.flush()) throw std::runtime_error("Cannot write crouchwalk archive");
        output.close();
        read(temporary);
        if (std::filesystem::exists(backup)) throw std::runtime_error("Previous crouchwalk import backup needs recovery");
        const bool had_target = std::filesystem::exists(target);
        if (had_target) std::filesystem::rename(target, backup);
        try { std::filesystem::rename(temporary, target); }
        catch (...) {
            if (had_target) std::filesystem::rename(backup, target);
            throw;
        }
        if (had_target) std::filesystem::remove(backup);
        return {};
    } catch (const std::exception &error) { return error.what(); }
}

inline void reset_stage() {
    resource = owner = state = motion = channel = 0;
    attempted = upper_ours = false;
    playing = -1;
}

inline bool owns(const psprecomp::GuestMemory &memory) {
    return playing >= 0 && memory.contains(motion, 8) && memory.contains(channel, 0x80) &&
           memory.load32(motion + 4) == channel && memory.load32(channel + 0x48) == archive_hash;
}

// Layer 1's own archive back, as far as it still holds ours.
inline void restore_upper(psprecomp::GuestMemory &memory) {
    if (upper_ours && memory.contains(channel, 0x100)) {
        if (memory.load32(channel + 0x84) == resource) memory.store32(channel + 0x84, saved_upper[0]);
        if (memory.load32(channel + 0xc8) == archive_hash) memory.store32(channel + 0xc8, saved_upper[1]);
    }
    upper_ours = false;
}

inline void restore(psprecomp::GuestMemory &memory) {
    if (owns(memory)) {
        const std::array<std::uint32_t, 3> at{motion, channel + 4, channel + 0x48};
        const std::array<std::uint32_t, 3> mine{resource, resource, archive_hash};
        for (unsigned i = 0; i < 3; ++i)
            if (memory.load32(at[i]) == mine[i]) memory.store32(at[i], saved[i]);
    }
    restore_upper(memory);
    playing = -1;
}

// The game asking for layer 1 (a request or a release, as when it aims or
// draws) gets its archive back first, so its clip index means its own clip.
inline void hand_back_upper(psprecomp::CorpusRuntime &corpus, Context &ctx) {
    if (upper_ours && !fulton_visuals::selecting_motion && ctx.gpr[4] == motion && ctx.gpr[5] == 1u)
        restore_upper(static_cast<Runtime &>(corpus).memory());
}
inline void request(psprecomp::CorpusRuntime &corpus, Context &ctx) {
    hand_back_upper(corpus, ctx);
    original_request(corpus, ctx);
}
inline void release(psprecomp::CorpusRuntime &corpus, Context &ctx) {
    hand_back_upper(corpus, ctx);
    original_release(corpus, ctx);
}

inline void prepare(Runtime &runtime, Context &ctx, Resume next) {
    if (attempted) { next(ctx); return; }
    attempted = true;
    portablekit::hle_extension::call_guest(ctx, engine + 0x3a478u, {0u, std::uint32_t(bytes.size()), 0u},
        [&runtime, next](auto &context, std::uint32_t buffer) {
            if (!buffer || !runtime.memory().contains(buffer, bytes.size())) {
                enabled = false; status = "Could not allocate crouchwalk archive"; next(context); return;
            }
            runtime.memory().copy_in(buffer, bytes);
            portablekit::hle_extension::call_guest(context, engine + 0x3bc8cu,
                {buffer, 0x08000000u | archive_hash, 1u, std::uint32_t(bytes.size()), 0u},
                [buffer, next](auto &loaded, std::uint32_t result) {
                    resource = result == 1 ? buffer : 0;
                    if (!resource) { enabled = false; status = "Crouchwalk resource loader failed"; }
                    next(loaded);
                });
        });
}

inline void start(Context &ctx, std::uint32_t block, int clip, std::uint32_t layer = 0, Resume next = {}) {
    // Match component 0x1676C: start the clip, then snapshot the previous
    // evaluated pose and blend it with 0x7F69C. The start offset is not a blend.
    // The upper layer takes the upper mask in both, as the game's own requests do.
    // Not the game's own request: Fulton must not replay it at its hand-back,
    // which would put the walking loop back on with nothing to stop it.
    const std::uint32_t mask = layer ? upper_mask : 0x7ffffu;
    fulton_visuals::selecting_motion = true;
    portablekit::hle_extension::call_guest(ctx, engine + 0x7f264u,
        {block, layer, std::uint32_t(clip), 0u, mask, 0u},
        [block, mask, layer, next](auto &started, std::uint32_t) {
            portablekit::hle_extension::call_guest(started, engine + 0x7f69cu,
                {block, blend_ticks, mask, 0u, layer},
                [next](auto &blended, std::uint32_t) {
                    fulton_visuals::selecting_motion = false;
                    if (next) next(blended);
                });
        });
}

inline void play(Runtime &runtime, Context &ctx, std::uint32_t player, int clip) {
    auto &memory = runtime.memory();
    const auto block = memory.load32(player + 0x1e8u);
    if (!memory.contains(block, 0xc0)) return;
    const auto layers = memory.load32(block + 4);
    if (!memory.contains(layers, 0x100)) return;
    if (playing == clip && motion == block && owns(memory)) return;
    if (playing < 0) {
        motion = block; channel = layers;
        saved = {memory.load32(motion), memory.load32(channel + 4), memory.load32(channel + 0x48)};
        old_clip = int(memory.load32(channel));
    }
    memory.store32(motion, resource);
    memory.store32(channel + 4, resource);
    memory.store32(channel + 0x48, archive_hash);
    playing = clip;
    const bool mk22 = has_upper && (upper_ours || memory.load32(channel + 0xc8) == mk22_po);
    std::cout << "[crouchwalk] " << (clip ? "slow" : "normal") << " loop" << (mk22 ? ", Mk22 upper body" : "")
              << std::endl;
    start(ctx, motion, clip, 0, [&runtime, clip, mk22](Context &next) {
        if (!mk22) return;
        auto &memory = runtime.memory();
        if (!upper_ours) {
            saved_upper = {memory.load32(channel + 0x84), memory.load32(channel + 0xc8)};
            old_upper = int(memory.load32(channel + 0x80));
            upper_ours = true;
        }
        memory.store32(channel + 0x84, resource);
        memory.store32(channel + 0xc8, archive_hash);
        start(next, motion, clip + 2, 1);
    });
}

inline void crouch_decision(psprecomp::CorpusRuntime &corpus, Context &ctx) {
    auto &runtime = static_cast<Runtime &>(corpus);
    auto &memory = runtime.memory();
    if (inner) { inner = false; original(corpus, ctx); return; }
    const auto player = ctx.gpr[4], work = ctx.gpr[5];
    if (!enabled || !memory.contains(player, 0xc00) || !memory.contains(work, 0x90) ||
        memory.load32(engine + 0x2f7060u) != player) { original(corpus, ctx); return; }
    owner = player; state = work;
    portablekit::hle_extension::call_guest(ctx, engine + 0xb2024u, {player},
        [&runtime, player, work](auto &context, std::uint32_t strength) {
            portablekit::hle_extension::call_guest(context, engine + 0xb1e84u, {player},
                [&runtime, player, work, strength](auto &before, std::uint32_t yaw) {
                    auto &memory = runtime.memory();
                    const auto first = memory.load16(player + 0x730), second = memory.load16(player + 0x73c);
                    // Only the native crouch decision sees zero movement. The
                    // collision/movement step receives the original records.
                    memory.store16(player + 0x730, 0); memory.store16(player + 0x73c, 0);
                    inner = true;
                    portablekit::hle_extension::call_guest(before, decision, {player, work},
                        [&runtime, player, work, strength, yaw, first, second](auto &after, std::uint32_t) {
                            auto &memory = runtime.memory();
                            memory.store16(player + 0x730, first); memory.store16(player + 0x73c, second);
                            if (memory.load32(work + 0x34) != idle || strength < 0x28u || strength > 255u) {
                                const bool was_ours = owns(memory), was_upper = upper_ours,
                                           staying = memory.load32(work + 0x34) == idle;
                                restore(memory);
                                if (was_ours && staying && old_clip >= 0)
                                    start(after, motion, old_clip, 0, [was_upper](Context &next) {
                                        if (was_upper && old_upper >= 0) start(next, motion, old_upper, 1);
                                    });
                                return;
                            }
                            // Fulton has the motion; its hand-back returns it.
                            if (fulton_visuals::holds(memory.load32(player + 0x1e8u))) return;
                            prepare(runtime, after, [&runtime, player, yaw](auto &ready) {
                                if (!resource) return;
                                auto &memory = runtime.memory();
                                memory.store16(player + 0x13a, std::uint16_t(yaw));
                                const bool slow = (memory.load32(player + 0x724) & 0x1000u) != 0;
                                play(runtime, ready, player, slow ? 1 : 0);
                            });
                        });
                });
        });
}

inline void move(psprecomp::CorpusRuntime &corpus, Context &ctx) {
    auto &runtime = static_cast<Runtime &>(corpus);
    auto &memory = runtime.memory();
    if (movement.inner) { movement.around(corpus, ctx, {}); return; }
    // The motion helper is a local goto inside this generated unit. Hook
    // the dispatched state component, only for its movement phase (0x20).
    if (ctx.gpr[6] != 0x20u) { movement.original(corpus, ctx); return; }
    if (ctx.gpr[4] != owner || !owns(memory) || !memory.contains(state, 0x38) || memory.load32(state + 0x34) != idle) {
        if (ctx.gpr[4] == owner) restore(memory);
        movement.original(corpus, ctx);
        return;
    }
    const auto actor = memory.load32(memory.load32(owner + 0x4c) + 0x28);
    const std::array<std::uint32_t, 2> at{engine + 0x2f70bcu + actor * 0x70u, owner + 0xb84u};
    const std::array<std::uint32_t, 2> old{memory.load32(at[0]) & 2u, memory.load32(at[1]) & 2u};
    // 0x106898 gates root displacement on standing bit 1. Borrow that bit
    // for the movement phase only; crouch bit 2 and its collision state stay set.
    for (auto address : at) memory.store32(address, memory.load32(address) | 2u);
    movement.around(corpus, ctx, [at, old](auto &runtime, std::uint32_t) {
        for (unsigned i = 0; i < 2; ++i)
            runtime.memory().store32(at[i], (runtime.memory().load32(at[i]) & ~2u) | old[i]);
    });
}

inline void stage_init(psprecomp::CorpusRuntime &runtime, Context &ctx) {
    reset_stage(); original_stage(runtime, ctx);
}

inline void module_ready(Runtime &runtime, std::string_view name, std::uint32_t base) {
    if (!enabled || name != "mgp_stage") return;
    reset_stage();
    original_stage = runtime.registered_function(base);
    if (original_stage) runtime.register_function(base, &stage_init, "po_crouchwalk_stage_init");
}

inline void install(Runtime &runtime) {
    const auto entries = portablekit::install::read_settings_file(portablekit::install::user_data_directory());
    const auto setting = entries.find("pw.crouchwalking");
    selected = setting != entries.end() && setting->second == "1";
    refresh();
    enabled = selected && available;
    if (!enabled) return;
    bytes = read(directory() / "crouchwalk.mtar");
    original = runtime.registered_function(decision);
    if (!original || !movement.install(runtime, locomotion, &move, "po_crouchwalk_movement")) {
        enabled = false; status = "Crouchwalk hooks unavailable"; return;
    }
    runtime.register_function(decision, &crouch_decision, "po_crouchwalk_decision");
    fulton_visuals::release_player_motion = [](psprecomp::GuestMemory &memory) { restore(memory); };
    // An archive imported with the Mk.22's upper body, and the clip request
    // and release to hand layer 1 back through (after Fulton's, if it has them).
    has_upper = fulton_convert::get(bytes, 8) == 4;
    original_request = runtime.registered_function(engine + 0x7f264u);
    original_release = runtime.registered_function(engine + 0x7f634u);
    if (has_upper && original_request && original_release) {
        runtime.register_function(engine + 0x7f264u, &request, "po_crouchwalk_motion_request");
        runtime.register_function(engine + 0x7f634u, &release, "po_crouchwalk_motion_release");
    } else has_upper = false;
    std::cout << "[crouchwalk] enabled" << (has_upper ? " with the Mk22 upper body" : "") << std::endl;
}
} // namespace mgs::crouchwalking
