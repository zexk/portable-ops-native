#pragma once

// What the player is doing, for anything that wants to show it (rich
// presence): the stage the engine was last asked to load, and a name for it.
// This is the local state only; nothing here talks to another program.
//
// The engine's stage request (PO 1.02 0x39A20, PO+ US 0x41498, EU 0x41FDC,
// JP 0x41430) takes the stage's directory name in a0. A request is not proof
// that the stage came up, so this is "loading or in", never "playing".
//
// Area names: every scenario carries one table of twenty in this order, the
// one the save list prints a save's place from. That entry N names stage
// st<N+1> is inferred, not traced: the eight names the twelve online maps
// carry are exactly entries 1, 2, 3, 5, 9, 12, 14 and 18, and n01, n02, n03,
// n05, n09, n12, n14 and n18 all exist. Not confirmed against the game.

#include "psprecomp/runtime.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace mgs::presence {

inline constexpr std::array<std::string_view, 20> kAreas{
    "Communications Base", "Soviet Patrol Base", "Supply Depot",     "Prison",       "Town",
    "Western Wilderness",  "Rail Bridge",        "Research Lab",     "Hospital",     "Plant",
    "Warhead Storage",     "Security Base",      "Ravine",           "Harbor",       "Guest House",
    "Airport",             "Power Substation",   "Silo Entrance",    "Silo Complex", "Launch Control Room",
};

// The area of a story stage (st05, st0501, st07tn, st10_boss) or an online
// map (n05), or empty for menus and anything unknown.
constexpr std::string_view area(std::string_view stage) {
    const std::size_t digits = stage.starts_with("st") ? 2u : stage.starts_with("n") ? 1u : 0u;
    if (digits == 0u || stage.size() < digits + 2u) return {};
    unsigned number = 0;
    for (const char c : stage.substr(digits, 2u)) {
        if (c < '0' || c > '9') return {};
        number = number * 10u + static_cast<unsigned>(c - '0');
    }
    return number >= 1u && number <= kAreas.size() ? kAreas[number - 1u] : std::string_view{};
}
constexpr bool online(std::string_view stage) { return stage.starts_with("n") && !area(stage).empty(); }

struct State {
    std::string stage;  // the directory name last requested, or empty
    std::uint64_t changes{};
};
inline State state;
inline psprecomp::Runtime::RecompiledFunction original_request = nullptr;

inline void request(psprecomp::CorpusRuntime &runtime, psprecomp::AllegrexContext &ctx) {
    // Bounded: stage names are at most a dozen characters.
    std::string name = static_cast<psprecomp::Runtime &>(runtime).memory().read_c_string(ctx.gpr[4], 32u);
    if (name != state.stage) {
        state.stage = std::move(name);
        ++state.changes;
        const std::string_view label = area(state.stage);
        std::cout << "[presence] stage " << state.stage;
        if (!label.empty()) std::cout << " (" << label << (online(state.stage) ? ", online" : "") << ')';
        std::cout << std::endl;
    }
    original_request(runtime, ctx);
}

inline void install(psprecomp::Runtime &runtime, std::uint32_t stage_request) {
    const psprecomp::Runtime::RecompiledFunction original = runtime.registered_function(stage_request);
    if (original == nullptr || original == &request) return;
    original_request = original;
    runtime.register_function(stage_request, &request, "mgs_stage_request");
}

} // namespace mgs::presence

static_assert(mgs::presence::area("st01") == "Communications Base" && mgs::presence::area("st0501") == "Town" &&
              mgs::presence::area("st10_boss") == "Plant" && mgs::presence::area("st20") == "Launch Control Room" &&
              mgs::presence::area("n18") == "Silo Entrance" && mgs::presence::online("n18") &&
              mgs::presence::area("title").empty() && mgs::presence::area("ntmenu2").empty() &&
              mgs::presence::area("st21").empty() && mgs::presence::area("st").empty());
