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
    std::cout << "Camera follow, manual orbit, recentre and rule restoration checks passed\n";
}
