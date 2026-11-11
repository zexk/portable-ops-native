#pragma once

// The second stick as the engine's own camera stick.
//
// Read from the executable: the player's input step clears the player's camera
// stick (+0x73e x, +0x73f y as signed bytes, +0x73c strength, +0x740 world
// angle, +0x744 length) every frame and fills it from the D-pad, or from the
// analog nub in the control types that put the camera on it. Everything camera-side reads only that
// stick: the third-person camera turns its orbit and changes its height from
// it with its own dead zone, smoothing and wall handling, and first person
// looks with it. So the second stick is written there after the input step,
// with the same arithmetic the game applies to the nub, and under the same
// conditions the game would clear it. The camera then behaves exactly as the
// game's own analog camera does.

#include "camera/camera_input.hpp"
#include "guest_wrap.hpp"
#include "portablekit/hle_extension.hpp"
#include "profile.hpp"
#include "settings/settings.hpp"

#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <numbers>
#include <optional>

namespace mgs {

struct CameraAddresses {
    std::uint32_t player_input;  // the player's input step, called with the player in a0
    std::uint32_t action_flags;  // per-actor action bitsets, 0x1c words each
    std::uint32_t pad;           // the engine's pad state: +0x26 movement yaw
    std::uint32_t globals;       // where the engine keeps the pointer to its control settings
    // The game's own flag setter, called where one is given (PO+, whose setter
    // checks the local player's flags against checksums and hangs on a mismatch).
    std::uint32_t set_action = 0u;
};

struct CameraState {
    CameraAddresses addresses;
    psprecomp::Runtime::RecompiledFunction player_input = nullptr;
};
inline CameraState camera_state;

[[nodiscard]] inline bool action(psprecomp::GuestMemory &memory, std::uint32_t player, std::uint32_t flag) {
    const std::uint32_t actor = memory.load32(memory.load32(player + 0x4cu) + 0x28u);
    const std::uint32_t word = camera_state.addresses.action_flags + 4u * (actor * 0x1cu + (flag >> 5u));
    return (memory.load32(word) >> (flag & 31u) & 1u) != 0u;
}

// The camera's own reading of a stick byte: nothing within 0x40 of centre, then
// twice the rest, up to 126.
[[nodiscard]] inline float camera_input(float byte) { return std::copysign(std::max(std::fabs(byte) - 64.0f, 0.0f) * 2.0f, byte); }

// A stick byte the camera reads as `input`, for adding to what a stick asked.
[[nodiscard]] inline float camera_byte(float input) {
    input = std::clamp(input, -126.0f, 126.0f);
    return input == 0.0f ? 0.0f : std::copysign(64.0f + std::fabs(input) / 2.0f, input);
}

// The third-person orbit keeps 0.625 of its turn each frame and adds
// input / 128 * 341.33 (0x10dd08 in PO), so a held input turns it by about
// 7.1 units (of 65536 a turn) a frame. Mouse and touch motion becomes the
// input that would turn the camera as far in a frame; their vertical motion
// is scaled alike and moves the camera as the stick's up and down does.
constexpr float kUnitsPerInput = 341.3333f / 128.0f / (1.0f - 0.625f);
constexpr float kInputPerDegree = 65536.0f / 360.0f / kUnitsPerInput;

// Sets an action flag as the game does (0xcb58c in PO): in the shared bitset
// and in the actor's own copy at +0xb84 (the actor table sits 0x5c before it).
// PO+ keeps the copy at +0xbb0 and checksums both for the local player, so
// there the game's setter (index, flags..., -1) runs on a copy of the context.
inline void set_action(psprecomp::Runtime &runtime, const psprecomp::AllegrexContext &ctx, std::uint32_t player,
                       std::uint32_t flag) {
    auto &memory = runtime.memory();
    const std::uint32_t index = memory.load32(memory.load32(player + 0x4cu) + 0x28u);
    if (camera_state.addresses.set_action != 0u) {
        psprecomp::AllegrexContext call = ctx;
        call.gpr[4] = index;
        call.gpr[5] = flag;
        call.gpr[6] = 0xffffffffu;
        call.gpr[31] = 0u;
        (void)runtime.invoke_isolated_aot(camera_state.addresses.set_action, call);
        return;
    }
    const std::uint32_t bit = 1u << (flag & 31u);
    const std::uint32_t word = camera_state.addresses.action_flags + 4u * (index * 0x1cu + (flag >> 5u));
    memory.store32(word, memory.load32(word) | bit);
    const std::uint32_t actor = memory.load32(camera_state.addresses.action_flags - 0x5cu + 4u * index);
    if (actor == 0u) return;
    const std::uint32_t own = actor + 0xb84u + 4u * (flag >> 5u);
    memory.store32(own, memory.load32(own) | bit);
}

inline void feed_camera_stick(psprecomp::Runtime &runtime, const psprecomp::AllegrexContext &ctx, std::uint32_t player) {
    auto &memory = runtime.memory();
    using portablekit::camera::Source;
    const portablekit::camera::Rate stick = portablekit::camera::rate(Source::Stick);
    const portablekit::camera::Turn mouse = portablekit::camera::take(Source::Mouse);
    const portablekit::camera::Turn touch = portablekit::camera::take(Source::Touch);
    // What the camera could not take last frame (it turns at most 126 a frame)
    // comes first, so a quick swipe turns it as far, only over more frames.
    static float carried_yaw = 0.0f;
    static float carried_pitch = 0.0f;
    const float motion_yaw = carried_yaw + (mouse.yaw_degrees + touch.yaw_degrees) * kInputPerDegree;
    const float motion_pitch = carried_pitch + (mouse.pitch_degrees + touch.pitch_degrees) * kInputPerDegree;
    carried_yaw = 0.0f;
    carried_pitch = 0.0f;
    if (stick.yaw == 0.0f && stick.pitch == 0.0f && motion_yaw == 0.0f && motion_pitch == 0.0f) return;
    const std::uint32_t pad = camera_state.addresses.pad;
    // Where the game clears the camera stick, it stays clear.
    if (action(memory, player, 0x13bu) || action(memory, player, 0x79u)) return;
    if (action(memory, player, 0xbau) && !action(memory, player, 7u)) return;

    // The nub's arithmetic: a byte each way from centre, at most -127, the
    // length over 127 at most 1, and a strength of 255 times that, nothing below 0x30.
    float x = std::max(std::round(stick.yaw * 127.0f), -127.0f);
    float y = std::max(std::round(stick.pitch * 127.0f), -127.0f);
    const auto add_motion = [](float &byte, float motion, float &carried) {
        if (motion == 0.0f) return;
        const float wanted = camera_input(byte) + motion;
        const float taken = std::clamp(wanted, -126.0f, 126.0f);
        // At most two frames' worth waits, so a spin faster than the camera
        // turns stops with the mouse instead of running on.
        carried = std::clamp(wanted - taken, -252.0f, 252.0f);
        byte = std::round(camera_byte(taken));
    };
    add_motion(x, motion_yaw, carried_yaw);
    add_motion(y, motion_pitch, carried_pitch);
    float length = std::hypot(x, y) / 127.0f;
    if (length > 1.0f) {
        x /= length;
        y /= length;
        length = 1.0f;
    }
    int strength = static_cast<int>(length * 255.0f);
    if (strength < 0x30) strength = 0;
    strength = std::min(strength, 0xff);
    memory.store8(player + 0x73eu, static_cast<std::uint8_t>(static_cast<std::int8_t>(x)));
    memory.store8(player + 0x73fu, static_cast<std::uint8_t>(static_cast<std::int8_t>(y)));
    if (strength > 0) {
        const auto movement_yaw = static_cast<std::int16_t>(memory.load16(pad + 0x26u));
        const auto angle = static_cast<std::int16_t>(std::atan2(x, y) * 32768.0f / std::numbers::pi_v<float> + 0.5f);
        memory.store16(player + 0x740u, static_cast<std::uint16_t>(angle + movement_yaw));
    }
    memory.store32(player + 0x744u, std::bit_cast<std::uint32_t>(length));
    memory.store16(player + 0x73cu, static_cast<std::uint16_t>(strength));
    // What the input step does last with the camera stick it filled: mark it
    // held, and while it turns sideways have the movement's reference follow
    // the camera, so Snake walks where the turned camera looks.
    if (x != 0.0f || y != 0.0f) memory.store32(player + 0x44u, memory.load32(player + 0x44u) | 0x40u);
    if (static_cast<std::int8_t>(x) != 0) set_action(runtime, ctx, player, 0xd7u);
}

// The stick is read where the game reads its own; motion sources (mouse,
// touch) have nowhere to go yet and are dropped rather than left to build up.
// Whether first person reads the two sticks the other way round, as the
// game's choosers (look: 0xb20f4, move: 0xb1cec in PO) decide it.
[[nodiscard]] inline bool first_person_swapped(psprecomp::GuestMemory &memory, std::uint32_t player) {
    const auto flag = [&](std::uint32_t f) { return action(memory, player, f); };
    if (flag(0x148u)) return false;
    // 0x16 also selects the camera stick for native D-pad locomotion outside
    // first person. Swapping there makes L + D-pad fight that movement state.
    if (!flag(0xbau) && !flag(7u)) return false;
    if (flag(0x16u)) return true;
    const std::uint32_t globals = memory.load32(camera_state.addresses.globals);
    if ((memory.load32(globals + 4u) & 0x80000u) != 0u) return false;
    return (memory.load32(player + 0x724u) & memory.load32(globals + 0x6cu)) != 0u || !flag(0x4fu);
}

// The movement stick (+0x730..) and the camera stick (+0x73c..) trade places:
// x and y, strength, world angle and length.
inline void swap_sticks(psprecomp::GuestMemory &memory, std::uint32_t player) {
    const auto swap8 = [&](std::uint32_t a, std::uint32_t b) {
        const std::uint8_t t = memory.load8(player + a);
        memory.store8(player + a, memory.load8(player + b));
        memory.store8(player + b, t);
    };
    const auto swap16 = [&](std::uint32_t a, std::uint32_t b) {
        const std::uint16_t t = memory.load16(player + a);
        memory.store16(player + a, memory.load16(player + b));
        memory.store16(player + b, t);
    };
    const std::uint32_t length = memory.load32(player + 0x738u);
    swap8(0x732u, 0x73eu);
    swap8(0x733u, 0x73fu);
    swap16(0x730u, 0x73cu);
    swap16(0x734u, 0x740u);
    memory.store32(player + 0x738u, memory.load32(player + 0x744u));
    memory.store32(player + 0x744u, length);
}

// First-person look (component 0x10A4D8 in PO, its stick code at 0x10B4AC)
// reads the stick one axis at a time. Within 0x23 of centre an axis gives
// nothing; beyond it the square of the rest is a small turn rate (work
// +0x8C/+0x8E) and also feeds a second, accelerating pair (+0x88/+0x8A) that
// climbs to the same cap on each axis however far the stick is pushed. That
// pair is most of the turn (1092 against 180 a frame), so any held direction
// ends as a pure diagonal or a pure axis: eight directions. Two corrections
// make the game's own arithmetic follow the stick: the bytes of the stick the
// look chooser picks are rewritten so each axis' rate is its share of the
// stick's direction, and each axis' accelerating turn is held to that share
// of the cap, times the square of how far the stick is pushed.
inline float aim_share[2] = {1.0f, 1.0f};  // x, y: 1 leaves the game's cap alone
inline std::uint32_t aim_player = 0u;      // whose stick the shares are from
inline std::uint32_t aim_strength = 0u;    // that stick's strength word

inline void radial_aim(psprecomp::GuestMemory &memory, std::uint32_t bytes) {
    const float axis[2] = {static_cast<float>(static_cast<std::int8_t>(memory.load8(bytes))),
                           static_cast<float>(static_cast<std::int8_t>(memory.load8(bytes + 1u)))};
    // The nub's own dead zone (strength below 0x30 of 255), taken off the length.
    constexpr float dead = 48.0f / 255.0f;
    const float length = std::min(std::hypot(axis[0], axis[1]) / 127.0f, 1.0f);
    const float push = std::max(length - dead, 0.0f) / (1.0f - dead);
    for (unsigned i = 0; i < 2; ++i) {
        // This axis' share of the direction, and of the cap: the push squared, as the rate is.
        const float part = push == 0.0f ? 0.0f : std::min(std::fabs(axis[i]) / std::hypot(axis[0], axis[1]), 1.0f);
        aim_share[i] = part * push * push;
        const float value = part == 0.0f ? 0.0f : std::copysign(std::round(35.0f + 92.0f * std::sqrt(part) * push), axis[i]);
        memory.store8(bytes + i, static_cast<std::uint8_t>(static_cast<std::int8_t>(value)));
    }
}

inline void player_input(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    auto &memory = static_cast<psprecomp::Runtime &>(rt).memory();
    const std::uint32_t player = ctx.gpr[4];
    camera_state.player_input(rt, ctx);
    const portablekit::settings::Settings &settings = portablekit::settings::current();
    if (settings.analog_camera) feed_camera_stick(static_cast<psprecomp::Runtime &>(rt), ctx, player);
    if (settings.swap_sticks_first_person && first_person_swapped(memory, player)) swap_sticks(memory, player);
    if (player == aim_player) aim_share[0] = aim_share[1] = 1.0f;
    // <prefix>_NO_RADIAL_AIM keeps the game's own first-person response.
    static const bool radial = portablekit::hle_extension::env("NO_RADIAL_AIM") == nullptr;
    if (!action(memory, player, 0xbau) && !action(memory, player, 7u)) return;
    const std::uint32_t bytes = player + (first_person_swapped(memory, player) ? 0x732u : 0x73eu);
    // <prefix>_TRACE_AIM: both sticks as the game left them and the look stick after the rewrite.
    static const bool trace = portablekit::hle_extension::env("TRACE_AIM") != nullptr;
    const auto s8 = [&](std::uint32_t at) { return int(static_cast<std::int8_t>(memory.load8(at))); };
    const int raw[4] = {s8(player + 0x732u), s8(player + 0x733u), s8(player + 0x73eu), s8(player + 0x73fu)};
    if (radial) {
        radial_aim(memory, bytes);
        aim_player = player;
        aim_strength = bytes - 2u;
    }
    if (trace && (raw[0] || raw[1] || raw[2] || raw[3]))
        std::cout << "[aim] move " << raw[0] << ' ' << raw[1] << " camera " << raw[2] << ' ' << raw[3] << " look@"
                  << std::hex << (bytes - player) << std::dec << ' ' << s8(bytes) << ' ' << s8(bytes + 1u) << std::endl;
}

// The look component, entered with the player, its work and a phase. Before
// its movement phase (0x20) the accelerating turn the last frame left is held
// to each axis' share of the cap (step * 0x16C / 5, or 0x222 with flag 0xDD).
// With a weapon aimed (flag 0x2E) the game runs that turn only while the look
// stick's strength is 0xC9 or more, a step from about 90 a frame to 1000; the
// strength is raised for the length of the call, so the turn grows with the
// push all the way instead.
struct LookState {
    GuestWrap component;
    std::uint32_t step = 0u;  // the engine's frame step
};
inline LookState look_state;

inline void look_component(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    auto &memory = static_cast<psprecomp::Runtime &>(rt).memory();
    const std::uint32_t player = ctx.gpr[4], work = ctx.gpr[5];
    if (look_state.component.inner) {
        look_state.component.around(rt, ctx, {});
        return;
    }
    if (ctx.gpr[6] != 0x20u || !memory.contains(player, 0xc00u) || !memory.contains(work, 0xc0u)) {
        look_state.component.original(rt, ctx);
        return;
    }
    const auto turn = [&](std::uint32_t at) { return static_cast<int>(static_cast<std::int16_t>(memory.load16(work + at))); };
    static const bool trace = portablekit::hle_extension::env("TRACE_AIM") != nullptr;
    if (trace && (turn(0x8cu) || turn(0x8eu) || turn(0x88u) || turn(0x8au)))
        std::cout << "[look] rate " << turn(0x8eu) << ' ' << turn(0x8cu) << " accel " << turn(0x8au) << ' '
                  << turn(0x88u) << " angle " << turn(0x72u) << ' ' << turn(0x70u) << " share " << aim_share[0] << ' '
                  << aim_share[1] << (player == aim_player ? " own" : " other") << std::endl;
    if (player != aim_player || (aim_share[0] >= 1.0f && aim_share[1] >= 1.0f)) {
        look_state.component.original(rt, ctx);
        return;
    }
    const int cap = static_cast<int>(memory.load32(look_state.step)) * (action(memory, player, 0xddu) ? 0x222 : 0x16c) / 5;
    for (unsigned i = 0; i < 2; ++i) {
        const std::uint32_t at = i == 0 ? 0x8au : 0x88u;  // x turns through +0x8A, y through +0x88
        const int limit = static_cast<int>(static_cast<float>(cap) * std::min(aim_share[i], 1.0f));
        memory.store16(work + at, static_cast<std::uint16_t>(std::clamp(turn(at), -limit, limit)));
    }
    const std::uint32_t strength = aim_strength;
    const std::uint16_t held = memory.load16(strength);
    if (held == 0u || held >= 0xc9u) {
        look_state.component.original(rt, ctx);
        return;
    }
    memory.store16(strength, 0xc9u);
    look_state.component.around(rt, ctx, [strength, held](psprecomp::Runtime &runtime, std::uint32_t) {
        if (runtime.memory().load16(strength) == 0xc9u) runtime.memory().store16(strength, held);
    });
}

inline void install_look(psprecomp::Runtime &runtime, std::uint32_t component, std::uint32_t step) {
    if (!look_state.component.install(runtime, component, &look_component, "mgs_look_component")) return;
    look_state.step = step;
    std::cout << "[look] first-person turn follows the stick at 0x" << std::hex << component << std::dec << std::endl;
}

inline void camera_frame(psprecomp::Runtime &) { portablekit::camera::discard(); }

inline bool camera_driving() { return camera_state.player_input != nullptr; }

inline void install_camera(psprecomp::Runtime &runtime, CameraAddresses addresses) {
    const psprecomp::Runtime::RecompiledFunction original = runtime.registered_function(addresses.player_input);
    if (original == nullptr || original == &player_input) return;
    camera_state.addresses = addresses;
    camera_state.player_input = original;
    runtime.register_function(addresses.player_input, &player_input, "mgs_player_input");
    std::cout << "[camera-stick] second stick feeds the camera stick from 0x" << std::hex << addresses.player_input
              << std::dec << std::endl;
}

// An instruction with what relocation or a different data layout changes
// (jump targets, address halves and offsets) taken out.
[[nodiscard]] constexpr std::uint32_t code_shape(std::uint32_t word) {
    switch (word >> 26u) {
    case 0x02: case 0x03: return word & 0xfc000000u;
    case 0x08: case 0x09: case 0x0d: case 0x0f: case 0x20: case 0x21: case 0x23: case 0x24: case 0x25:
    case 0x28: case 0x29: case 0x2b: case 0x31: case 0x32: case 0x36: case 0x39: case 0x3a: case 0x3e:
        return word & 0xffff0000u;
    default: return word;
    }
}

// Where exactly one place in `code` has `shape`, each word compared after
// `shape_of`; nullopt where none or several do.
template <typename ShapeOf>
[[nodiscard]] std::optional<std::uint32_t> find_shape(std::span<const std::uint8_t> code,
                                                      std::span<const std::uint32_t> shape, ShapeOf shape_of) {
    const auto word = [&](std::size_t at) {
        std::uint32_t value = 0u;
        std::memcpy(&value, code.data() + at, sizeof(value));
        return shape_of(value);
    };
    std::optional<std::uint32_t> found;
    for (std::size_t at = 0; at + 4u * shape.size() <= code.size(); at += 4u) {
        std::size_t k = 0;
        while (k < shape.size() && word(at + 4u * k) == shape[k]) ++k;
        if (k < shape.size()) continue;
        if (found) return std::nullopt;  // more than one: not this
        found = static_cast<std::uint32_t>(at);
    }
    return found;
}

// PO+ keeps the player's code in each stage module, at a different place in
// each: the input step is found by the shape of its first instructions, and
// only where exactly one place has it.
inline void install_camera_in_module(psprecomp::Runtime &runtime, std::uint32_t base,
                                     std::span<const std::uint8_t> code, std::span<const std::uint32_t> shape,
                                     std::uint32_t action_flags, std::uint32_t pad, std::uint32_t globals,
                                     std::uint32_t set_action) {
    if (const auto found = find_shape(code, shape, code_shape))
        install_camera(runtime, {base + *found, action_flags, pad, globals, set_action});
}

// The camera's own swing behind the player (Settings::camera_follow).
//
// Read from the executable: in its ordinary mode the camera turns itself
// towards the player's facing in one routine, by a rule each stage's camera
// actor sets in its parameters (+0x68; parameter 'C'): 0 never, 1 when the
// player faces well away from the camera and the camera stick is idle, 2
// whenever the stick is idle. The same routine also runs the recentre on its
// button. Off runs it with the rule at 0, so the camera stays where the
// player put it and the button still recentres it. Wrap the actor's update,
// not its swing helper: generated same-unit calls bypass registered helpers.
struct CameraFollowState {
    psprecomp::Runtime::RecompiledFunction update = nullptr;
};
inline CameraFollowState camera_follow_state;

inline void camera_update(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    if (portablekit::settings::current().camera_follow) {
        camera_follow_state.update(rt, ctx);
        return;
    }
    auto &memory = static_cast<psprecomp::Runtime &>(rt).memory();
    const std::uint32_t rule = ctx.gpr[4] + 0x68u;
    const std::uint32_t stage_rule = memory.load32(rule);
    memory.store32(rule, 0u);
    camera_follow_state.update(rt, ctx);
    memory.store32(rule, stage_rule);
}

inline void install_camera_follow(psprecomp::Runtime &runtime, std::uint32_t update) {
    const psprecomp::Runtime::RecompiledFunction original = runtime.registered_function(update);
    if (original == nullptr || original == &camera_update) return;
    camera_follow_state.update = original;
    runtime.register_function(update, &camera_update, "mgs_camera_update");
    std::cout << "[camera-follow] the camera's follow rule is controlled at update 0x" << std::hex
              << update << std::dec << std::endl;
}

// The camera actor's update in PO+ US 1.01 stage modules, with relocated
// jump targets removed. Other releases are used only if this matches uniquely.
inline constexpr std::uint32_t kCameraUpdateShape[] = {
    0x27bdfff0u, 0xafb00000u, 0xafbf0004u, 0x0c000000u, 0x00808021u, 0x10400007u, 0x00000000u,
    0x0c000000u, 0x02002021u, 0x8fbf0004u, 0x8fb00000u, 0x03e00008u, 0x27bd0010u};

inline void install_camera_follow_in_module(psprecomp::Runtime &runtime, std::uint32_t base,
                                            std::span<const std::uint8_t> code) {
    const auto relocated_out = [](std::uint32_t word) {
        switch (word >> 26u) {
        case 0x02u: case 0x03u: return word & 0xfc000000u;
        default: return word;
        }
    };
    if (const auto found = find_shape(code, kCameraUpdateShape, relocated_out))
        install_camera_follow(runtime, base + *found);
}

// The look component's first twelve instructions, branch offsets removed. PO+
// keeps it in each stage module, the same in every release.
inline constexpr std::uint32_t kLookComponentShape[] = {0x27bdfff0u, 0x24020020u, 0xafb20008u, 0x00009021u,
                                                        0xafb10004u, 0x00a08821u, 0xafb00000u, 0x00808021u,
                                                        0x10c20000u, 0xafbf000cu, 0x28c20021u, 0x10400000u};

inline void install_look_in_module(psprecomp::Runtime &runtime, std::uint32_t base,
                                   std::span<const std::uint8_t> code, std::uint32_t step) {
    const auto branches_out = [](std::uint32_t word) { return word >> 26u == 0x04u ? word & 0xffff0000u : word; };
    if (const auto found = find_shape(code, kLookComponentShape, branches_out)) install_look(runtime, base + *found, step);
}

inline constexpr portablekit::CameraDriver kCameraDriver{
    .frame = &camera_frame,
    .driving = &camera_driving,
    .swaps_first_person = true,
    .follows = true,
};

} // namespace mgs
