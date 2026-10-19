#pragma once

// The engine's third-person camera, turned from the second stick, the mouse and
// touch.
//
// Read from the executable: the engine keeps six camera slots of 0x530 bytes;
// slot 0 is the player's. A slot's word at +8 points at the camera mode it is
// following (the word at +4 of a mode is the mode's id), and each frame the
// slot eases its eye (+0x30) and target (+0x40) toward the mode's, then builds
// the view from them. The third-person mode keeps its orbit out of reach (it
// lives in each stage's own code), so the port turns what the mode asks for
// instead: the camera system's update (reached through the actor table, so a
// wrapper takes effect) is wrapped, and for its duration the mode's eye is
// swung around its target by the port's own yaw and its yaw turned to match,
// then both are put back. The slot eases toward the swung eye, so the turn is
// smooth, and the yaw the player's movement is read against is taken from the
// mode's inside the update, so up on the stick still walks into the picture. Angles are 65536 a turn; yaw is atan2(x, z) of
// target minus eye.

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
    std::uint32_t player_slot;       // camera slot 0
    std::uint32_t third_person_mode; // that mode's id
    std::uint32_t update;            // the camera system's per-frame update
};

struct CameraState {
    CameraAddresses addresses;
    psprecomp::Runtime::RecompiledFunction update = nullptr;
    bool driving = false;
    float yaw = 0.0f;    // the port's turn, radians in the engine's sense
    float pitch = 0.0f;  // the port's raising of the eye, radians
};
inline CameraState camera_state;

[[nodiscard]] inline bool third_person(psprecomp::GuestMemory &memory) {
    const std::uint32_t mode = memory.load32(camera_state.addresses.player_slot + 8u);
    return mode != 0u && memory.load32(mode + 4u) == camera_state.addresses.third_person_mode;
}

// Once per game frame: what the player asked for since the last one becomes
// more turn. Anywhere but third person the turn is dropped, so the camera comes
// back behind the player the way the game puts it.
inline void camera_frame(psprecomp::Runtime &runtime) {
    camera_state.driving = camera_state.update != nullptr && third_person(runtime.memory());
    if (!camera_state.driving) {
        portablekit::camera::discard();
        camera_state.yaw = 0.0f;
        camera_state.pitch = 0.0f;
        return;
    }
    constexpr float kRadians = std::numbers::pi_v<float> / 180.0f;
    const portablekit::camera::Turn turn = portablekit::camera::take();
    // Positive yaw turns right, which lowers the engine's angle; positive
    // pitch looks down, which raises the eye.
    camera_state.yaw =
        std::remainder(camera_state.yaw - turn.yaw_degrees * kRadians, 2.0f * std::numbers::pi_v<float>);
    camera_state.pitch += turn.pitch_degrees * kRadians;
}

inline bool camera_driving() { return camera_state.driving; }

inline void camera_update(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    auto &memory = static_cast<psprecomp::Runtime &>(rt).memory();
    const bool turn = (camera_state.yaw != 0.0f || camera_state.pitch != 0.0f) && third_person(memory);
    if (!turn) {
        camera_state.update(rt, ctx);
        return;
    }
    const std::uint32_t slot = camera_state.addresses.player_slot;
    const std::uint32_t mode = memory.load32(slot + 8u);
    const std::uint32_t eye = mode + 0x20u;
    const std::uint32_t target = mode + 0x30u;
    const std::uint32_t yaw = mode + 0x72u;
    const std::uint32_t saved[3] = {memory.load32(eye), memory.load32(eye + 4u), memory.load32(eye + 8u)};
    const std::uint16_t mode_yaw = memory.load16(yaw);
    const auto f = [&](std::uint32_t address) { return std::bit_cast<float>(memory.load32(address)); };
    const auto put = [&](std::uint32_t address, float value) { memory.store32(address, std::bit_cast<std::uint32_t>(value)); };

    // From the target back to the eye, as a horizontal distance, a height and
    // a direction.
    const float dx = f(eye) - f(target);
    const float dy = f(eye + 4u) - f(target + 4u);
    const float dz = f(eye + 8u) - f(target + 8u);
    const float across = std::hypot(dx, dz);
    const float distance = std::hypot(across, dy);
    if (across > 1.0f) {
        // The game's own elevation plus the port's, kept between looking
        // almost level and almost straight down.
        const float game = std::atan2(dy, across);
        const float lowest = -10.0f * std::numbers::pi_v<float> / 180.0f;
        const float highest = 75.0f * std::numbers::pi_v<float> / 180.0f;
        const float elevation = std::clamp(game + camera_state.pitch, lowest, highest);
        camera_state.pitch = elevation - game;
        const float scale = distance * std::cos(elevation) / across;
        const float c = std::cos(camera_state.yaw);
        const float s = std::sin(camera_state.yaw);
        put(eye, f(target) + (dx * c + dz * s) * scale);
        put(eye + 4u, f(target + 4u) + distance * std::sin(elevation));
        put(eye + 8u, f(target + 8u) + (dz * c - dx * s) * scale);
        const auto units =
            static_cast<std::int32_t>(std::lround(camera_state.yaw * (32768.0f / std::numbers::pi_v<float>)));
        memory.store16(yaw, static_cast<std::uint16_t>(mode_yaw + units));
    }
    camera_state.update(rt, ctx);
    // The slot copies the mode's eye, and the mode starts its next one from
    // the slot's: both go back, or the next frame would turn a turned eye.
    for (std::uint32_t i = 0; i < 3u; ++i) {
        memory.store32(eye + 4u * i, saved[i]);
        memory.store32(slot + 0x30u + 4u * i, saved[i]);
    }
    memory.store16(yaw, mode_yaw);
}

inline void install_camera(psprecomp::Runtime &runtime, CameraAddresses addresses) {
    camera_state.addresses = addresses;
    camera_state.update = runtime.registered_function(addresses.update);
    if (camera_state.update != nullptr) runtime.register_function(addresses.update, &camera_update, "mgs_camera_update");
}

inline constexpr portablekit::CameraDriver kCameraDriver{
    .frame = &camera_frame,
    .driving = &camera_driving,
};

} // namespace mgs
