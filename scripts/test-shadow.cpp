// Build like test-widescreen.cpp, linking psprecomp_core src/*.o.
#include "../common/host/detail_distance.hpp"
#include <cassert>
#include <vector>
#include <fstream>
#include <iterator>

const portablekit::input::Bindings &portablekit::input::default_bindings() {
    static const Bindings bindings{};
    return bindings;
}
std::string portablekit::settings::game_default_name() { return {}; }

portablekit::settings::Settings &portablekit::settings::current() {
    static Settings settings;
    return settings;
}

// Synchronous guest-return harness for the LOD wrapper. The real HLE waits
// for the guest function to return before invoking the same continuation.
psprecomp::Runtime *active_runtime;
void portablekit::hle_extension::call_guest(psprecomp::AllegrexContext &ctx, std::uint32_t address,
                                           std::span<const std::uint32_t>, GuestReturn after) {
    active_runtime->registered_function(address)(*active_runtime, ctx);
    after(ctx, ctx.gpr[2]);
}

int main(int argc, char **argv) {
    psprecomp::Runtime runtime;
    auto &memory = runtime.memory();
    auto &settings = portablekit::settings::current();
    std::vector<std::uint32_t> words(std::begin(mgs::detail_distance::kShadowFadeShape),
                                     std::end(mgs::detail_distance::kShadowFadeShape));
    // Negative low halves exercise signed MIPS address reconstruction.
    constexpr std::uint32_t start = 0x08868000u;
    for (auto index : {0, 7, 15}) words[index] |= 0x0887u;
    for (auto index : {2, 12, 14}) words[index] |= 0x8000u;
    words[8] |= 0x8004u;
    words[16] |= 0x8008u;
    const auto code = [&] {
        return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(words.data()), words.size() * 4u);
    };
    const auto reset = [&] {
        memory.store32(start, std::bit_cast<std::uint32_t>(5000.0f));
        memory.store32(start + 4u, std::bit_cast<std::uint32_t>(10000.0f));
        memory.store32(start + 8u, std::bit_cast<std::uint32_t>(1.0f));
    };
    reset();
    settings.detail_distance_percent = 400;
    mgs::detail_distance::shadow_in_module(runtime, code());
    assert(mgs::detail_distance::shadow_fade == start);
    // Optional linked code and flat PRX load segments, extracted read-only
    // from the US disc. Scalar data needs no relocation for this check.
    if (argc > 1) {
        assert(argc == 3);
        const auto read = [](const char *path) {
            std::ifstream file(path, std::ios::binary);
            assert(file);
            return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)), {});
        };
        const auto stage_code = read(argv[1]), data = read(argv[2]);
        constexpr std::uint32_t base = 0x08B10600u;
        assert(memory.contains(base, data.size()));
        for (std::size_t i = 0; i < data.size(); ++i) memory.store8(base + i, data[i]);
        settings.detail_distance_percent = 400;
        mgs::detail_distance::shadow_in_module(runtime, stage_code);
        const auto fade = mgs::detail_distance::shadow_fade;
        assert(fade != 0u);
        assert(std::bit_cast<float>(memory.load32(fade)) == 20000.0f);
        assert(std::bit_cast<float>(memory.load32(fade + 4u)) == 40000.0f);
        return 0;
    }
    assert(std::bit_cast<float>(memory.load32(start)) == 20000.0f);
    assert(std::bit_cast<float>(memory.load32(start + 4u)) == 40000.0f);
    mgs::detail_distance::shadow_in_module(runtime, code());
    assert(std::bit_cast<float>(memory.load32(start)) == 20000.0f);
    settings.detail_distance_percent = 100;
    mgs::detail_distance::shadow_in_module(runtime, code());
    assert(std::bit_cast<float>(memory.load32(start)) == 5000.0f);
    mgs::detail_distance::shadow_in_module(runtime, {});
    assert(mgs::detail_distance::shadow_fade == 0u);
    const auto unique = words;
    words.insert(words.end(), unique.begin(), unique.end());
    mgs::detail_distance::shadow_in_module(runtime, code());
    assert(mgs::detail_distance::shadow_fade == 0u);
    words = unique;
    words[8] += 4u; // End pointer must be adjacent to start.
    mgs::detail_distance::shadow_in_module(runtime, code());
    assert(mgs::detail_distance::shadow_fade == 0u);
    words = unique;
    memory.store32(start, std::bit_cast<std::uint32_t>(7.0f));
    mgs::detail_distance::shadow_in_module(runtime, code());
    assert(mgs::detail_distance::shadow_fade == 0u);
    assert(std::bit_cast<float>(memory.load32(start)) == 7.0f);
    reset();
    mgs::detail_distance::shadow_in_module(runtime, code());
    assert(mgs::detail_distance::shadow_fade == start);
    std::cout << "Stage shadow discovery and scaling checks passed\n";
    constexpr std::uint32_t table = 0x08870000u, handler = 0x08871000u;
    active_runtime = &runtime;
    runtime.register_function(handler, +[](psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
        auto &mem = static_cast<psprecomp::Runtime &>(rt).memory();
        mem.store32(table, 2u);
        mem.store32(table + 16u, std::bit_cast<std::uint32_t>(49.0f));
        mem.store32(table + 20u, std::bit_cast<std::uint32_t>(144.0f));
        ctx.gpr[2] = 17u;
    }, "test_lod");
    // The second record is untouched by the handler and must stay unchanged.
    memory.store32(table + 36u, std::bit_cast<std::uint32_t>(81.0f));
    mgs::detail_distance::install(runtime, handler, table, start);
    psprecomp::AllegrexContext ctx{};
    for (auto percent : {400u, 400u, 100u}) {
        settings.detail_distance_percent = percent;
        runtime.registered_function(handler)(runtime, ctx);
        const float factor = percent * percent / 10000.0f;
        assert(std::bit_cast<float>(memory.load32(table + 16u)) == 49.0f * factor);
        assert(std::bit_cast<float>(memory.load32(table + 20u)) == 144.0f * factor);
        assert(std::bit_cast<float>(memory.load32(table + 36u)) == 81.0f);
        assert(std::bit_cast<float>(memory.load32(start)) == 50.0f * percent);
        assert(ctx.gpr[2] == 17u);
        assert(!mgs::detail_distance::handler.inner);
    }
    std::cout << "LOD registration and shadow refresh checks passed\n";
}
