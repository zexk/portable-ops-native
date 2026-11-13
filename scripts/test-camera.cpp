// After building a port, run from the repository root:
// c++ -std=c++20 -O2 -Iportablekit/include -Iportablekit/host scripts/test-camera.cpp \
//     out/po/portablekit/CMakeFiles/psprecomp_core.dir/src/*.o -o out/test-camera
// out/test-camera
// Optional existing stage images: out/test-camera out/stages-pop/*.bin
#include "../common/host/camera.hpp"

#include <cassert>
#include <vector>
#include <fstream>
#include <iterator>

const portablekit::input::Bindings &portablekit::input::default_bindings() {
    static const Bindings bindings{};
    return bindings;
}
const char *portablekit::hle_extension::env(const char *) { return nullptr; }
std::filesystem::path portablekit::install::user_data_directory() { return {}; }
portablekit::install::SettingsEntries portablekit::install::read_settings_file(const std::filesystem::path &) { return {}; }
portablekit::camera::Rate portablekit::camera::rate(Source) { return {}; }
portablekit::camera::Turn portablekit::camera::take(Source) { return {}; }
// Stand-in for the runtime's guest call: re-enter the address, then report
// the return as the real stub does once the guest function has returned.
psprecomp::Runtime *guest_runtime = nullptr;
void portablekit::hle_extension::call_guest(psprecomp::AllegrexContext &ctx, std::uint32_t address,
                                           std::span<const std::uint32_t>, GuestReturn after) {
    assert(guest_runtime != nullptr && "Discovery tests must not execute guest callbacks");
    guest_runtime->registered_function(address)(*guest_runtime, ctx);
    after(ctx, ctx.gpr[2]);
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

int main(int argc, char **argv) {
    psprecomp::Runtime runtime;
    // PO+ discovers the follow hook in every newly loaded stage. Missing or
    // ambiguous signatures must leave the registered function untouched.
    std::vector<std::uint32_t> words(std::begin(mgs::kCameraUpdateShape),
                                     std::end(mgs::kCameraUpdateShape));
    words[3] |= 0x00123456u; // Relocated JAL targets are deliberately ignored.
    words[7] |= 0x00234567u;
    const auto bytes = [&] {
        return std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t *>(words.data()), words.size() * 4u);
    };
    for (const auto base : {0x08820000u, 0x08830000u}) {
        runtime.register_function(base, &update, "stage_update");
        const auto first = words.front();
        words.front() = 0u;
        mgs::install_camera_follow_in_module(runtime, base, bytes());
        assert(runtime.registered_function(base) == &update);
        words.front() = first;
        const auto unique = words;
        words.insert(words.end(), unique.begin(), unique.end());
        mgs::install_camera_follow_in_module(runtime, base, bytes());
        assert(runtime.registered_function(base) == &update);
        words = unique;
        mgs::install_camera_follow_in_module(runtime, base, bytes());
        assert(runtime.registered_function(base) == &mgs::camera_update);
        assert(mgs::camera_follow_state.update.original == &update);
        mgs::install_camera_follow_in_module(runtime, base, bytes());
        assert(mgs::camera_follow_state.update.original == &update);
    }
    auto &memory = runtime.memory();
    // Exercise the input and look installers too, including replacement of
    // generated code at the same guest address after a stage reload.
    const auto &input_shape = mgs::kPopPlayerInputShape;
    for (bool look : {false, true}) {
        const auto shape = look ? std::span<const std::uint32_t>(mgs::kLookComponentShape)
                                : std::span<const std::uint32_t>(input_shape);
        words.assign(shape.begin(), shape.end());
        if (look) words[8] |= 0x123u; // Branch displacement is ignored.
        else words[0] |= 0xff90u;    // Stack displacement is ignored.
        const auto unique = words;
        const auto hook = look ? &mgs::look_component : &mgs::player_input;
        for (auto base : {0x08840000u, 0x08850000u, 0x08850000u}) {
            runtime.register_function(base, &update, "replacement_stage");
            const auto install = [&] {
                if (look) mgs::install_look_in_module(runtime, base, bytes(), base + 0x1000u);
                else mgs::install_camera_in_module(runtime, base, bytes(), input_shape,
                                                   1u, 2u, 3u, 4u);
            };
            words[0] = 0;
            install();
            assert(runtime.registered_function(base) == &update);
            words = unique;
            words.insert(words.end(), unique.begin(), unique.end());
            install();
            assert(runtime.registered_function(base) == &update);
            words = unique;
            install();
            install();
            assert(runtime.registered_function(base) == hook);
            if (look) {
                assert(mgs::look_state.component.original == &update);
                assert(mgs::look_state.component.address == base);
                assert(mgs::look_state.step == base + 0x1000u);
            } else {
                assert(mgs::camera_state.player_input == &update);
                assert(mgs::camera_state.addresses.player_input == base);
                assert(mgs::camera_state.addresses.set_action == 4u);
            }
        }
    }
    psprecomp::AllegrexContext ctx{};
    ctx.gpr[4] = kCamera;
    ctx.pc = kUpdate;
    runtime.register_function(kUpdate, &update, "generated_update");
    guest_runtime = &runtime;
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
    // Stalking adds only a synthetic held bit; real button edges and other
    // game-written bits must survive its removal on the next input step.
    mgs::analog_stalk::enabled = true;
    mgs::analog_stalk::threshold = 50; // limit = 191 with the game's dead zone
    for (auto strength : {0u, 128u, 190u, 191u, 255u}) {
        memory.store32(player + 0x724u, 0x20u);
        memory.store32(player + 0x728u, 0x40u);
        memory.store32(player + 0x72cu, 0x80u);
        memory.store16(player + 0x730u, strength);
        mgs::analog_stalk::after(memory, player);
        assert(memory.load32(player + 0x724u) ==
               (0x20u | (strength > 0u && strength < 191u ? mgs::analog_stalk::kStalk : 0u)));
        mgs::analog_stalk::before(memory, player);
        assert(memory.load32(player + 0x724u) == 0x20u);
        assert(memory.load32(player + 0x728u) == 0x40u);
        assert(memory.load32(player + 0x72cu) == 0x80u);
    }
    memory.store16(player + 0x730u, 128u);
    memory.store32(player + 0x724u, mgs::analog_stalk::kStalk);
    mgs::analog_stalk::after(memory, player);
    mgs::analog_stalk::before(memory, player);
    assert(memory.load32(player + 0x724u) == mgs::analog_stalk::kStalk);
    memory.store32(player + 0x724u, 0u);
    mgs::analog_stalk::after(memory, player);
    memory.store32(player + 0x724u, 0x80u); // Game replaced the held word.
    mgs::analog_stalk::before(memory, player);
    assert(memory.load32(player + 0x724u) == 0x80u);
    mgs::analog_stalk::enabled = false;
    mgs::analog_stalk::after(memory, player);
    assert(memory.load32(player + 0x724u) == 0x80u);
    // Optional linked-stage images: exercise the exact production matcher,
    // and reject partial camera coverage. Menus may
    // legitimately contain none of these three gameplay components.
    for (int argument = 1; argument < argc; ++argument) {
        std::ifstream file(argv[argument], std::ios::binary);
        assert(file && "Cannot open stage image");
        const std::vector<std::uint8_t> code((std::istreambuf_iterator<char>(file)), {});
        const auto input = mgs::find_shape(code, mgs::kPopPlayerInputShape, mgs::code_shape);
        const auto follow = mgs::find_shape(code, mgs::kCameraUpdateShape, [](std::uint32_t word) {
            return word >> 26u == 2u || word >> 26u == 3u ? word & 0xfc000000u : word;
        });
        const auto look = mgs::find_shape(code, mgs::kLookComponentShape, [](std::uint32_t word) {
            return word >> 26u == 4u ? word & 0xffff0000u : word;
        });
        std::cout << argv[argument] << " input=" << input.has_value()
                  << " follow=" << follow.has_value() << " look=" << look.has_value() << '\n';
        assert(input.has_value() == follow.has_value() && input.has_value() == look.has_value());
    }
    std::cout << "Camera follow and first-person stick selection checks passed\n";
}
