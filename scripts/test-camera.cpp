// After building a port, run from the repository root:
// c++ -std=c++20 -O2 -Iportablekit/include -Iportablekit/host scripts/test-camera.cpp \
//     out/po/portablekit/CMakeFiles/psprecomp_core.dir/src/*.o -o out/test-camera
// out/test-camera
#include "../common/host/camera.hpp"

#include <cassert>

const portablekit::input::Bindings &portablekit::input::default_bindings() {
    static const Bindings bindings{};
    return bindings;
}
const char *portablekit::hle_extension::env(const char *) { return nullptr; }
std::string portablekit::settings::game_default_name() { return {}; }
portablekit::settings::Settings &portablekit::settings::current() {
    static Settings settings;
    return settings;
}

constexpr std::uint32_t kCamera = 0x08800100u;
constexpr std::uint32_t kUpdate = 0x08801000u;
int manual_turn = 0;
bool recentre = false;

// Model a generated update that calls its swing helper directly, bypassing
// runtime registrations for that helper, as the game's same-unit calls do.
void swing(psprecomp::GuestMemory &memory) {
    unsigned yaw = memory.load16(kCamera + 0x328u);
    if (memory.load32(kCamera + 0x68u) != 0u) yaw += 100u;
    yaw += manual_turn;
    if (recentre) yaw = 0u;
    memory.store16(kCamera + 0x328u, yaw);
}
void update(psprecomp::CorpusRuntime &runtime, psprecomp::AllegrexContext &ctx) {
    swing(static_cast<psprecomp::Runtime &>(runtime).memory());
    ctx.pc = ctx.gpr[31];
}

int main() {
    psprecomp::Runtime runtime;
    auto &memory = runtime.memory();
    psprecomp::AllegrexContext ctx{};
    ctx.gpr[4] = kCamera;
    ctx.pc = kUpdate;
    runtime.register_function(kUpdate, &update, "generated_update");
    mgs::install_camera_follow(runtime, kUpdate);
    mgs::install_camera_follow(runtime, kUpdate); // Installing twice keeps the original.
    memory.store32(kCamera + 0x68u, 2u);
    const auto step = [&](bool follow, int turn, bool centre, unsigned expected) {
        portablekit::settings::current().camera_follow = follow;
        manual_turn = turn;
        recentre = centre;
        runtime.registered_function(kUpdate)(runtime, ctx);
        assert(memory.load16(kCamera + 0x328u) == expected);
        assert(memory.load32(kCamera + 0x68u) == 2u);
    };
    step(true, 0, false, 100u);
    step(false, 0, false, 100u); // Movement cannot turn the camera.
    step(false, 25, false, 125u); // Manual orbit still works.
    step(false, 0, true, 0u); // The recentre button still works.
    step(true, 0, false, 100u); // Re-enabling restores the stage's follow rule.

    constexpr std::uint32_t player = 0x08810000u, control = 0x08812000u;
    constexpr std::uint32_t flags = 0x08813000u, globals = 0x08814000u, settings = 0x08815000u;
    mgs::camera_state.addresses.action_flags = flags;
    mgs::camera_state.addresses.globals = globals;
    memory.store32(player + 0x4cu, control);
    memory.store32(control + 0x28u, 0u);
    memory.store32(globals, settings);
    memory.store32(settings + 0x6cu, 0x200u);
    const auto flag = [&](unsigned id, bool on) {
        const auto word = flags + 4u * (id >> 5u);
        const auto bit = 1u << (id & 31u);
        memory.store32(word, on ? memory.load32(word) | bit : memory.load32(word) & ~bit);
    };
    flag(0x16u, true);
    assert(!mgs::first_person_swapped(memory, player)); // Native L + D-pad movement.
    flag(0xbau, true);
    assert(mgs::first_person_swapped(memory, player)); // The same selector in first person.
    flag(0x16u, false);
    flag(0x4fu, true);
    memory.store32(player + 0x724u, 0x200u);
    assert(mgs::first_person_swapped(memory, player)); // Held first-person button.
    memory.store32(player + 0x724u, 0u);
    assert(!mgs::first_person_swapped(memory, player)); // The game's chooser keeps sticks native.
    memory.store32(player + 0x724u, 0x200u);
    memory.store32(settings + 4u, 0x80000u);
    assert(!mgs::first_person_swapped(memory, player)); // Native control setting disables the swap.
    memory.store32(settings + 4u, 0u);
    flag(0x148u, true);
    assert(!mgs::first_person_swapped(memory, player)); // Special-state exception still wins.
    // Radial aim: what the game's per-axis look makes of the rewritten bytes,
    // (|byte| - 35)^2 a side, points along the stick and grows with its length.
    const auto aimed = [&](int x, int y) {
        memory.store8(player + 0x73eu, std::uint8_t(x));
        memory.store8(player + 0x73fu, std::uint8_t(y));
        mgs::radial_aim(memory, player + 0x73eu);
        const auto rate = [&](std::uint32_t at) {
            const int b = std::int8_t(memory.load8(at)), rest = std::max(std::abs(b) - 35, 0);
            return b < 0 ? -rest * rest : rest * rest;
        };
        return std::pair{rate(player + 0x73eu), rate(player + 0x73fu)};
    };
    assert(aimed(20, 10) == std::pair(0, 0));       // inside the dead zone
    assert(aimed(127, 0) == std::pair(92 * 92, 0)); // full push: the game's full rate
    const auto shallow = aimed(100, 25);            // the game alone drops y (25 < 35)
    assert(shallow.second > 0 && std::abs(shallow.first / double(shallow.second) - 4.0) < 0.5);
    const auto back = aimed(-60, -60);
    assert(back.first == back.second && back.first < 0);
    std::cout << "Camera follow and first-person stick selection checks passed\n";
}
