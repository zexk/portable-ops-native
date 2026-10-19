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

#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace mgs {

struct CameraAddresses {
    std::uint32_t player_input;  // the player's input step, called with the player in a0
    std::uint32_t action_flags;  // per-actor action bitsets, 0x1c words each
    std::uint32_t pad;           // the engine's pad state: +0x26 movement yaw
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

inline void player_input(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    auto &memory = static_cast<psprecomp::Runtime &>(rt).memory();
    const std::uint32_t player = ctx.gpr[4];
    camera_state.player_input(rt, ctx);

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
inline void camera_frame(psprecomp::Runtime &) { portablekit::camera::discard(); }

inline bool camera_driving() { return camera_state.player_input != nullptr; }

inline void install_camera(psprecomp::Runtime &runtime, CameraAddresses addresses) {
    camera_state.addresses = addresses;
    camera_state.player_input = runtime.registered_function(addresses.player_input);
    if (camera_state.player_input != nullptr)
        runtime.register_function(addresses.player_input, &player_input, "mgs_player_input");
}

inline constexpr portablekit::CameraDriver kCameraDriver{
    .frame = &camera_frame,
    .driving = &camera_driving,
};

} // namespace mgs
