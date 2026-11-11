#pragma once

// Game > Controls: stalk by tilting the stick a little. The game stalks
// (and our crouchwalk takes its slow loop) while the stalk button is held,
// which it reads from the player's held word at +0x724 (0x1000, Triangle in
// the mapped word). The input step (0xB12F8 in PO) derives that word and,
// from it and the one before, the pressed (+0x728) and released (+0x72C)
// words. After the step the stalk bit is added to the held word while the
// movement stick (+0x730 strength, 255 at full tilt) is tilted but below
// the threshold. Before the next step the word is put back as the game left
// it, so a real press of the button still reads as a press.

#include "install/user_data.hpp"
#include "portablekit/hle_extension.hpp"
#include "psprecomp/runtime.hpp"

#if defined(PORTABLEKIT_HAS_RENDERER)
#include "ui/widgets.hpp"
#endif

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

namespace mgs::analog_stalk {
constexpr std::uint32_t kStalk = 0x1000u;
constexpr int kDefaultThreshold = 50;

inline bool enabled = false;
inline int threshold = kDefaultThreshold;  // percent of full tilt
inline bool loaded = false;
inline std::uint32_t added_to = 0u;  // the player whose held word has the bit added
inline std::uint32_t game_held = 0u;  // that word as the game left it

inline void load() {
    if (loaded) return;
    loaded = true;
    try {
        const auto entries = portablekit::install::read_settings_file(portablekit::install::user_data_directory());
        if (const auto found = entries.find("input.analog_stalk"); found != entries.end()) enabled = found->second == "1";
        if (const auto found = entries.find("input.analog_stalk_threshold"); found != entries.end())
            threshold = std::clamp(std::stoi(found->second), 5, 95);
    } catch (const std::exception &) {
    }
}

inline void save() {
    try {
        const auto root = portablekit::install::user_data_directory();
        auto entries = portablekit::install::read_settings_file(root);
        entries["input.analog_stalk"] = enabled ? "1" : "0";
        entries["input.analog_stalk_threshold"] = std::to_string(threshold);
        portablekit::install::write_settings_file(root, entries);
    } catch (const std::exception &error) {
        std::cerr << "[stalk] cannot save setting: " << error.what() << std::endl;
    }
}

// Before the input step: the held word back as the game left it.
inline void before(psprecomp::GuestMemory &memory, std::uint32_t player) {
    if (added_to != player) return;
    added_to = 0u;
    if (memory.load32(player + 0x724u) == (game_held | kStalk)) memory.store32(player + 0x724u, game_held);
}

// After it: the stalk bit while the stick is tilted below the threshold.
// Movement strength is never between 0 and 128: a tilt that small reads as
// none (TRACE_STALK in a user-controlled trial on 2026-11-11 went from 174
// to 128 to 0). The threshold is a share of the range from 128 to full tilt,
// the travel the player can use; the host's own dead zone is rescaled out
// before the game sees the nub.
constexpr std::uint32_t kGameDeadZone = 128u;
inline void after(psprecomp::GuestMemory &memory, std::uint32_t player) {
    if (!enabled) return;
    const std::uint32_t held = memory.load32(player + 0x724u);
    const std::uint32_t strength = memory.load16(player + 0x730u);
    const std::uint32_t limit =
        kGameDeadZone + static_cast<std::uint32_t>(threshold) * (255u - kGameDeadZone) / 100u;
    // <prefix>_TRACE_STALK: the movement strength whenever it changes.
    static const bool trace = portablekit::hle_extension::env("TRACE_STALK") != nullptr;
    static std::uint32_t traced = 0u;
    if (trace && strength != traced) {
        traced = strength;
        std::cout << "[stalk] strength " << strength << (strength != 0u && strength < limit ? " stalk" : "") << std::endl;
    }
    if (strength == 0u || strength >= limit || (held & kStalk) != 0u) return;
    game_held = held;
    added_to = player;
    memory.store32(player + 0x724u, held | kStalk);
}

inline void reset() {
    enabled = false;
    threshold = kDefaultThreshold;
    save();
}

inline void draw() {
#if defined(PORTABLEKIT_HAS_RENDERER)
    using namespace portablekit::ui;
    load();
    section("Controls");
    if (toggle_row("Analog stalking", enabled,
                   {false, {}, "Stalk by tilting the stick a little, as if holding Triangle; full tilt moves as "
                               "usual. Crouched, it takes the slow crouchwalk."})) {
        enabled = !enabled;
        save();
    }
    if (slider_row("Stalk below", threshold, 5, 95, 5, "%d%%",
                   {!enabled, {}, "How far the stick tilts before stalking turns into normal movement."}))
        save();
#endif
}
} // namespace mgs::analog_stalk
