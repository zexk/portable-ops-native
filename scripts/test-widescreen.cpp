// Exercise the real projection hook against a small, empty guest runtime.
// After building PortableKit's psprecomp_core in out/pk-tests, compile with
// -Iportablekit/include -Iportablekit/host and link its src/*.o core objects.
#include "../common/host/widescreen.hpp"

#include <cassert>
#include <cmath>

const portablekit::input::Bindings &portablekit::input::default_bindings() {
    static const Bindings bindings{};
    return bindings;
}
std::string portablekit::settings::game_default_name() { return {}; }

portablekit::settings::Settings &portablekit::settings::current() {
    static Settings settings;
    return settings;
}

constexpr std::uint32_t view = 0x08800000, scale = 0x08801000;
float observed_focal = 0, observed_scale = 0;

int main() {
    psprecomp::Runtime runtime;
    auto &memory = runtime.memory();
    psprecomp::AllegrexContext ctx{};
    ctx.gpr[4] = view;
    memory.store32(view + 0x1b0, 480);
    memory.store32(view + 0x1b4, 272);
    memory.store32(scale, std::bit_cast<std::uint32_t>(1.0f));
    mgs::widescreen_state.addresses.x_scale = scale;
    mgs::widescreen_state.build_view = [](auto &rt, auto &context) {
        auto &mem = static_cast<psprecomp::Runtime &>(rt).memory();
        observed_focal = context.fpr[12];
        observed_scale = std::bit_cast<float>(mem.load32(scale));
        mem.store32(view + 0x1c8, std::bit_cast<std::uint32_t>(context.fpr[12]));
    };
    const auto check = [&](float aspect, unsigned fov, float focal, float expected, float x) {
        mgs::widescreen_state.aspect = aspect;
        portablekit::settings::current().fov_percent = fov;
        ctx.fpr[12] = focal;
        mgs::build_view(runtime, ctx);
        assert(std::fabs(observed_focal - expected) < 0.00001f);
        assert(std::fabs(observed_scale - x) < 0.00001f);
        assert(std::bit_cast<float>(memory.load32(scale)) == 1.0f);
        assert(std::bit_cast<float>(memory.load32(view + 0x1c8)) == focal);
    };
    constexpr float psp = 480.0f / 272;
    check(psp, 100, 2, 2, 1); // Stock aspect must still permit FOV adjustment.
    check(psp, 150, 2, 2 / 1.5f, 1);
    check(psp, 150, 8, 8 / 1.5f, 1); // Relative aim zoom survives.
    check(32.0f / 9, 100, 2, 2, psp / (32.0f / 9));
    check(4.0f / 3, 125, 2, 2 * (4.0f / 3) / psp / 1.25f, psp / (4.0f / 3));
    memory.store32(view + 0x1b0, 240);
    check(psp, 150, 2, 2, 1); // Subviews keep their original projection.
    std::cout << "FOV, aspect, zoom, subview and projection restoration checks passed\n";
}
