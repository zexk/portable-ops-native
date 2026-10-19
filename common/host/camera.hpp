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

namespace mgs {

struct CameraAddresses {
    std::uint32_t player_input;  // the player's input step, called with the player in a0
    std::uint32_t action_flags;  // per-actor action bitsets, 0x1c words each
    std::uint32_t pad;           // the engine's pad state: +0x26 movement yaw
    std::uint32_t globals;       // where the engine keeps the pointer to its control settings
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

inline void feed_camera_stick(psprecomp::GuestMemory &memory, std::uint32_t player) {
    const portablekit::camera::Rate stick = portablekit::camera::rate(portablekit::camera::Source::Stick);
    if (stick.yaw == 0.0f && stick.pitch == 0.0f) return;
    const std::uint32_t pad = camera_state.addresses.pad;
    // Where the game clears the camera stick, it stays clear.
    if (action(memory, player, 0x13bu) || action(memory, player, 0x79u)) return;
    if (action(memory, player, 0xbau) && !action(memory, player, 7u)) return;

    // The nub's arithmetic: a byte each way from centre, at most -127, the
    // length over 127 at most 1, and a strength of 255 times that, nothing below 0x30.
    float x = std::max(std::round(stick.yaw * 127.0f), -127.0f);
    float y = std::max(std::round(stick.pitch * 127.0f), -127.0f);
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
}

// The stick is read where the game reads its own; motion sources (mouse,
// touch) have nowhere to go yet and are dropped rather than left to build up.
// Whether first person reads the two sticks the other way round, as the
// game's choosers (look: 0xb20f4, move: 0xb1cec in PO) decide it.
[[nodiscard]] inline bool first_person_swapped(psprecomp::GuestMemory &memory, std::uint32_t player) {
    const auto flag = [&](std::uint32_t f) { return action(memory, player, f); };
    if (flag(0x148u)) return false;
    if (flag(0x16u)) return true;
    if (!flag(0xbau) && !flag(7u)) return false;
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

inline void player_input(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    auto &memory = static_cast<psprecomp::Runtime &>(rt).memory();
    const std::uint32_t player = ctx.gpr[4];
    camera_state.player_input(rt, ctx);
    const portablekit::settings::Settings &settings = portablekit::settings::current();
    if (settings.analog_camera) feed_camera_stick(memory, player);
    if (settings.swap_sticks_first_person && first_person_swapped(memory, player)) swap_sticks(memory, player);
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

// PO+ keeps the player's code in each stage module, at a different place in
// each: the input step is found by the shape of its first instructions, and
// only where exactly one place has it.
inline void install_camera_in_module(psprecomp::Runtime &runtime, std::uint32_t base,
                                     std::span<const std::uint8_t> code, std::span<const std::uint32_t> shape,
                                     std::uint32_t action_flags, std::uint32_t pad, std::uint32_t globals) {
    const auto word = [&](std::size_t at) {
        std::uint32_t value = 0u;
        std::memcpy(&value, code.data() + at, sizeof(value));
        return code_shape(value);
    };
    std::size_t found = code.size();
    for (std::size_t at = 0; at + 4u * shape.size() <= code.size(); at += 4u) {
        std::size_t k = 0;
        while (k < shape.size() && word(at + 4u * k) == shape[k]) ++k;
        if (k < shape.size()) continue;
        if (found != code.size()) return;  // more than one: not this
        found = at;
    }
    if (found == code.size()) return;
    install_camera(runtime, {base + static_cast<std::uint32_t>(found), action_flags, pad, globals});
}

inline constexpr portablekit::CameraDriver kCameraDriver{
    .frame = &camera_frame,
    .driving = &camera_driving,
    .swaps_first_person = true,
};

} // namespace mgs
