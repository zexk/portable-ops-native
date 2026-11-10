// No game or window required. Exercise selection and capture commit rules.
#include "../profiles/po/host/fulton.hpp"
#include <cassert>
#include <fstream>
#include <map>
#include <vector>

// Queue guest returns so motion hand-back is exercised across callbacks.
struct GuestCall {
    std::uint32_t function;
    std::vector<std::uint32_t> arguments;
    portablekit::hle_extension::GuestReturn returned;
};
std::vector<GuestCall> guest_calls;
const char *portablekit::hle_extension::env(const char *) { return nullptr; }
void portablekit::hle_extension::call_guest(AllegrexContext &, std::uint32_t function,
                                           std::span<const std::uint32_t> arguments, GuestReturn returned) {
    guest_calls.push_back({function, {arguments.begin(), arguments.end()}, std::move(returned)});
}

struct Memory {
    std::map<std::uint32_t, std::uint32_t> words;
    std::vector<char> ram;
    bool contains(std::uint32_t at, std::size_t length) const {
        return at >= 0x08000000u && length <= 0x02000000u && at <= 0x0a000000u - length;
    }
    std::uint32_t load32(std::uint32_t at) const {
        const auto found = words.find(at);
        if (found != words.end()) return found->second;
        if (!ram.empty() && contains(at, 4)) {
            std::uint32_t value;
            std::memcpy(&value, ram.data() + at - 0x08000000u, 4);
            return value;
        }
        return 0;
    }
    std::uint8_t load8(std::uint32_t at) const { return load32(at & ~3u) >> ((at & 3u) * 8u); }
    std::uint16_t load16(std::uint32_t at) const { return load32(at & ~3u) >> ((at & 2u) * 8u); }
    void store32(std::uint32_t at, std::uint32_t value) { words[at] = value; }
    void store16(std::uint32_t at, std::uint16_t value) {
        const auto shift = (at & 2u) * 8u;
        store32(at & ~3u, (load32(at & ~3u) & ~(0xffffu << shift)) | std::uint32_t(value) << shift);
    }
};

struct TestRuntime : psprecomp::CorpusRuntime {
    TestRuntime() : CorpusRuntime(nullptr, nullptr) {}
};
unsigned resets = 0;

int main(int argc, char **argv) {
    using namespace mgs::fulton;
    Memory memory;
    {
        std::vector<std::uint8_t> bank(0x200);
        auto put = [&](unsigned at, std::uint32_t value) { std::memcpy(bank.data() + at, &value, 4); };
        put(0, 0x01445053); put(4, bank.size()); put(bank.size() - 4, 0x01445045);
        put(8, 0x30); put(12, 0xb0); put(16, 0xd0); put(24, 0x180);
        for (unsigned i = 0; i < 4; ++i) {
            put(0x30 + i * 32 + 8, i * 8);
            put(0xb0 + i * 8, i * 0x20);
            put(0xd0 + i * 0x20, 16);
        }
        assert(mgs::fulton_sound::valid_foley(bank));
        assert(!mgs::fulton_sound::valid_bank(bank, 0x44, 0x9000));
        put(0xd0, 0xfffffff0);
        assert(!mgs::fulton_sound::valid_foley(bank));
        put(0xd0, 16); put(0xb4, 0xffffffff);
        assert(!mgs::fulton_sound::valid_foley(bank));
        put(0xb4, 0); put(0x38, 32);
        assert(!mgs::fulton_sound::valid_foley(bank));
    }
    {
        Memory source;
        constexpr std::uint32_t at = 0x08810000u, destination = 0x09900000u;
        constexpr unsigned old_sounds = 2971, old_banks = 324;
        const std::array<unsigned, 4> sizes{old_sounds * 8, old_banks * 16, old_banks * 4, old_banks * 2};
        source.store32(at, 0x53444853u);
        source.store32(at + 0xc, 0x0a570029u);
        source.store32(at + 0x10, 283);
        unsigned offset = 0x34;
        for (unsigned table = 0; table < sizes.size(); ++table) {
            source.store32(at + 0x14 + table * 4, at + offset);
            for (unsigned i = 0; i < sizes[table]; i += 4)
                source.store32(at + offset + i, 0x12340000u + table * 0x10000u + i);
            offset += sizes[table];
        }
        const auto extended = mgs::fulton_audio_catalog::extend(source, at, destination);
        assert(!extended.empty() && extended.size() <= 0x8000);
        auto word = [&](unsigned i) { std::uint32_t x; std::memcpy(&x, extended.data() + i, 4); return x; };
        for (unsigned table = 0; table < sizes.size(); ++table) {
            const auto old = source.load32(at + 0x14 + table * 4);
            const auto now = word(0x14 + table * 4) - destination;
            for (unsigned i = 0; i < sizes[table]; ++i)
                assert(extended[now + i] == source.load8(old + i));
        }
        assert(word(0x10) == 284 && word(0xc) == 0x0a5a0029u);
        const auto cues = word(0x14) - destination;
        assert(word(cues + 2971 * 8) == 0x40010103u);
        const auto banks = word(0x18) - destination;
        assert(word(banks + 324 * 16) == 0 && word(banks + 324 * 16 + 8) == 0x9000);
        const auto ids = word(0x20) - destination;
        assert((extended[ids + 648] | extended[ids + 649] << 8) == 2971);
        source.store32(at + 0x14, 0);
        assert(mgs::fulton_audio_catalog::extend(source, at, destination).empty());
        using mgs::fulton_visuals::Phase;
        using mgs::fulton_visuals::extra_sounds;
        unsigned played = 0;
        assert(extra_sounds(Phase::Attaching, 0, 75, true, played).empty());
        auto events = extra_sounds(Phase::Attaching, 35, 75, true, played);
        assert(events == (std::vector<std::uint32_t>{2971, 2972}));
        assert(extra_sounds(Phase::Attaching, 35, 75, true, played).empty());
        assert(extra_sounds(Phase::Attaching, 56, 75, true, played) == (std::vector<std::uint32_t>{2973}));
        assert(extra_sounds(Phase::Settle, 139, 350, false, played).empty());
        assert(extra_sounds(Phase::Settle, 140, 350, false, played) == (std::vector<std::uint32_t>{2974}));
        assert(extra_sounds(Phase::Settle, 141, 350, false, played).empty());
        played = 0;
        assert(extra_sounds(Phase::Attaching, 75, 75, false, played).empty());
    }
    constexpr std::uint32_t player = 0x08a50000u, work = 0x09b10000u, component = 0x09b20000u;
    constexpr Target target{work, 0x15f7f777u};
    memory.words[work] = target.name;
    memory.words[work + 0x10] = component;
    memory.words[work + 0x1438] = 79;

    mgs::fulton_visuals::state.enemy = work;
    mgs::fulton_visuals::state.yaw = 0xf123;
    memory.store32(work + 0x60, 0x1234abcd);
    memory.store32(work + 0x68, 0x5678dcba);
    mgs::fulton_visuals::face_player(memory);
    mgs::fulton_visuals::face_player(memory); // Preserve original headings across ticks.
    const auto turned = mgs::fulton_geometry::turn(mgs::fulton_geometry::turn(0x1234, 0xf123), 0xf123);
    assert(memory.load32(work + 0x60) == (std::uint32_t(turned) << 16 | 0xabcdu));
    assert(turned != 0x1234 && turned != 0xf123); // Gradual, shortest-arc rotation.
    assert(memory.load32(work + 0x68) == 0xf123dcba);
    memory.store16(work + 0x6a, 0x4321); // Engine takes ownership back.
    mgs::fulton_visuals::restore_facing(memory);
    assert(memory.load32(work + 0x60) == 0x1234abcd);
    assert(memory.load32(work + 0x68) == 0x4321dcba);
    mgs::fulton_visuals::state = {};
    {
        using namespace mgs::fulton_geometry;
        assert(turn(0xfff0, 0x0010) == 0xfff4); // Cross angle wrap in either direction.
        assert(turn(0x0010, 0xfff0) == 0x000c);
        assert(turn(0x1000, 0x1001) == 0x1001); // No one-unit stall.
        auto heading = std::uint16_t(0x8001);
        for (unsigned i = 0; i < 100; ++i) heading = turn(heading, 0x41ea);
        assert(heading == 0x41ea);
        // 90-degree Y bone: local -Z becomes world -X; local +Y stays +Y.
        const Matrix spine{0,0,-1,0, 0,1,0,0, 1,0,0,0, 1000,2000,3000,1};
        const auto anchor = transform(spine, {0,100,-200});
        assert(anchor == (Point{800,2100,3000}));
        // Prone bone rotated about X: local offsets must not become world-Y trims.
        const Matrix prone{1,0,0,0, 0,0,1,0, 0,-1,0,0, 1000,2000,3000,1};
        assert(transform(prone, {0,100,-200}) == (Point{1000,2200,3100}));
        const auto pose = harness(anchor, {800,2100,2900});
        const auto tip = transform(pose, {0,-1000,0});
        assert(std::abs(tip[0] - anchor[0]) < 0.01f);
        assert(std::abs(tip[1] - anchor[1]) < 1.0f);
        assert(tip[2] > anchor[2] + 999); // Effector points along intended aim.
        for (const auto point : {Point{800,2000,3000}, Point{800,2100,3000}})
            for (float value : harness(anchor, point)) assert(std::isfinite(value));
        const auto small = scaled(pose, 0.02f);
        assert(small[12] == anchor[0] && small[13] == anchor[1] && small[14] == anchor[2]);
        assert(small[15] == 1); // Growth must not scale translation or homogeneous W.
        constexpr std::uint32_t object = 0x09c10000, bones = 0x09c20000;
        memory.store32(object + 0xac, bones);
        assert(!bone(memory, object, 2)); // Uninitialized matrices are not usable.
        for (unsigned i = 0; i < 16; ++i)
            memory.store32(bones + 2 * 64 + i * 4, std::bit_cast<std::uint32_t>(spine[i]));
        assert(bone(memory, object, 2) == spine);
        memory.store32(bones + 2 * 64 + 8, std::bit_cast<std::uint32_t>(-1102.0f));
        assert(bone(memory, object, 2) == spine); // PO's scaled bases are normalized.
        memory.store32(bones + 2 * 64, 0x7fc00000u);
        assert(!bone(memory, object, 2)); // Never feed NaN poses to rendering.
        memory.store32(object + 0xac, 0);
        assert(!bone(memory, object, 2));
    }
    memory.words[work + 0x1850] = 23;
    memory.words[work + 0x1858] = 0x804;
    memory.words[component] = 0x1002;
    memory.words[component + 4] = target.name;
    memory.words[component + 0x24] = work;
    assert(eligible(memory, target));
    memory.words[work + 0x1858] = 0x10000040;
    memory.words[work + 0x3c0] = 0x01520152;
    assert(eligible(memory, target)); // Captured prone hold-up state.
    memory.words[work + 0x3c0] = 0x01510151;
    assert(!eligible(memory, target)); // Hold-up flag alone cannot admit standing actors.
    memory.words[work + 0x3c0] = 0x01520152;
    memory.words[work + 0x1858] |= 8;
    assert(!eligible(memory, target)); // Dead remains excluded, even with stale hold-up state.
    memory.words[work + 0x3c0] = 0;
    memory.words[work + 0x1858] = 0x804;
    for (auto bad : {0x800u, 0x884u, 0x805u}) {
        memory.words[work + 0x1858] = bad;
        assert(!eligible(memory, target)); // Awake, removed, dead.
    }
    memory.words[work + 0x1858] = 0x804;
    memory.words[work + 0x185c] = 0x02000000;
    assert(!eligible(memory, target)); // Already carried.
    memory.words[work + 0x185c] = 0;
    memory.words[work + 0x1850] = 3;
    assert(!eligible(memory, target)); // Scripted prisoner.
    memory.words[work + 0x1850] = 23;
    memory.words[work + 0x1438] = 0;
    assert(!eligible(memory, target)); // No capture procedure to run.
    memory.words[work + 0x1438] = 82;
    assert(eligible(memory, target)); // Procedure numbers differ by stage.
    memory.words[work + 0x1438] = 79;

    constexpr std::uint32_t node = 0x09b30000u;
    memory.words[engine + 0x258f8c] = node;
    memory.words[node + 0x10] = component;
    memory.words[work + 0x30] = std::bit_cast<std::uint32_t>(800.0f);
    assert(nearest(memory, player).work == work);
    assert(select(memory, player).work == 0); // The game does not offer a grab.
    memory.words[player + grab_flags] = grab_reach;
    assert(select(memory, player).work == work);
    memory.words[player + grab_flags] = 0;
    memory.words[work + 0x30] = std::bit_cast<std::uint32_t>(1000.0f);
    assert(nearest(memory, player).work == 0); // Past grab reach.
    memory.words[work + 0x30] = std::bit_cast<std::uint32_t>(3000.0f);
    assert(nearest(memory, player).work == 0);
    memory.words[work + 0x30] = 0x7fc00000; // NaN must never win selection.
    assert(nearest(memory, player).work == 0);
    memory.words[node] = node; // Corrupt/cyclic registry remains bounded.
    assert(nearest(memory, player).work == 0);

    constexpr std::uint32_t system = 0x08a10000u, record = system + 0x3ca0u;
    memory.words[record] = 0x40f;
    memory.words[record + 0xc] = 1;
    memory.words[record + 0x6c] = target.name;
    memory.words[record + 0x70] = 2;
    assert(!appended(memory, system, 0, target)); // Failed append cannot hide target.
    memory.words[system + 0x3c98] = 1;
    assert(appended(memory, system, 0, target));
    assert(!appended(memory, system, 80, target));
    memory.words[record + 0x6c] ^= 1;
    assert(!appended(memory, system, 0, target)); // Another actor's record.
    memory.words[record + 0x6c] = target.name;
    memory.words[record + 0x70] = 4;
    assert(!appended(memory, system, 0, target)); // Other recruitment source.

    std::vector<std::uint8_t> code(128);
    std::memcpy(code.data(), notify_shape.data(), notify_shape.size() * 4);
    assert(find(code, notify_shape) == 0);
    std::memcpy(code.data() + 64, notify_shape.data(), notify_shape.size() * 4);
    assert(!find(code, notify_shape)); // Ambiguous stage match must be rejected.
    code[0] ^= 1;
    assert(find(code, notify_shape) == 64);

    // Finished lift can still be rejected. Restore engine requests and release
    // an unused upper layer, while preserving archive words changed by engine.
    mgs::fulton_visuals::Override motion;
    constexpr std::uint32_t block = 0x09b40000u, channel = 0x09b50000u;
    memory.words[block + 4] = channel;
    memory.words[block] = 0x09900000u;
    assert(motion.capture(memory, block));
    motion.apply(memory, 0x09910000u, mgs::fulton_visuals::kLiftArchive);
    memory.words[channel + 0xc8] = 0x123456u; // Engine now owns this word.
    mgs::fulton_visuals::learned[block][0] = {0, 7, 0, 0x7ffff, 0};
    psprecomp::AllegrexContext context{};
    TestRuntime runtime;
    mgs::fulton_visuals::original_reset = [](auto &, auto &) { ++resets; };
    context.gpr[4] = block;
    context.gpr[5] = 1;
    context.gpr[31] = 0x08810000u;
    mgs::fulton_visuals::learned[block][1] = {1, 0x25, 0, 0x1ffe, 0};
    mgs::fulton_visuals::motion_reset(runtime, context);
    assert(resets == 1 && !mgs::fulton_visuals::learned[block][1]);
    // A release during the override is remembered but must not interrupt it.
    mgs::fulton_visuals::state.snake = motion;
    mgs::fulton_visuals::learned[block][1] = {1, 0x25, 0, 0x1ffe, 0};
    mgs::fulton_visuals::motion_reset(runtime, context);
    assert(resets == 1 && context.pc == context.gpr[31]);
    assert(!mgs::fulton_visuals::learned[block][1]);
    mgs::fulton_visuals::state = {};
    bool returned = false;
    auto lifetime = std::make_shared<int>(0);
    mgs::fulton_visuals::hand_back(memory, context, motion, [&, lifetime](auto &) { returned = true; });
    for (unsigned i = 0; i < 2; ++i) {
        assert(guest_calls.size() == 1 && !returned);
        auto call = std::move(guest_calls.back());
        guest_calls.clear();
        assert(call.function == engine + (i == 0 ? 0x7f264u : 0x7f634u));
        assert(call.arguments == (i == 0 ? std::vector<std::uint32_t>{block, 0, 7, 0, 0x7ffff, 0}
                                        : std::vector<std::uint32_t>{block, 1}));
        call.returned(context, 0);
    }
    assert(returned && !motion.active && !mgs::fulton_visuals::selecting_motion);
    assert(memory.load32(block) == 0x09900000u && memory.load32(channel + 0xc8) == 0x123456u);
    assert(guest_calls.empty());
    assert(lifetime.use_count() == 1); // Completion releases the callback chain.
    // Optional lift effects must layer through the game's normal sound call.
    mgs::fulton_sound::lift_current = true;
    returned = false;
    mgs::fulton_sound::play(memory, context, work + 0x30u, [&](auto &) { returned = true; }, mgs::fulton_sound::kLiftSound);
    assert(guest_calls.size() == 1 && !returned);
    auto effect = std::move(guest_calls.back());
    guest_calls.clear();
    assert(effect.function == mgs::fulton_sound::kPlay);
    assert(effect.arguments == (std::vector<std::uint32_t>{0x272u, 0, 0, work + 0x30u}));
    effect.returned(context, 1);
    assert(returned);
    // Appended ids exceed E540's hard-coded bound. Use the same listener and
    // positional sound2 calls directly, preserving arguments across returns.
    mgs::fulton_sound::foley_current = true;
    returned = false;
    mgs::fulton_sound::play(memory, context, work + 0x30u,
                            [&](auto &) { returned = true; }, mgs::fulton_sound::kFoleySound);
    const std::array<std::uint32_t, 3> functions{engine + 0x14a08u, engine + 0x1c5b68u, engine + 0x1c5b90u};
    const std::array<std::vector<std::uint32_t>, 3> arguments{{{}, {0x0888af60u}, {2971, work + 0x30u, 0, 0}}};
    for (unsigned i = 0; i < functions.size(); ++i) {
        assert(guest_calls.size() == 1 && !returned);
        auto call = std::move(guest_calls.back());
        guest_calls.clear();
        assert(call.function == functions[i] && call.arguments == arguments[i]);
        call.returned(context, i == 0 ? 0x0888af60u : 1u);
    }
    assert(returned);
    returned = false;
    mgs::fulton_sound::dispatch(context, 2974, 0,
                               [&](auto &, std::uint32_t handle) { returned = handle == 1; });
    assert(guest_calls.size() == 1);
    auto flat = std::move(guest_calls.back());
    guest_calls.clear();
    assert(flat.function == engine + 0x1c5ad8u);
    assert(flat.arguments == (std::vector<std::uint32_t>{2974, 0, 0, 0}));
    flat.returned(context, 1);
    assert(returned);
    mgs::fulton_sound::stage_changed();
    assert(!mgs::fulton_sound::current && !mgs::fulton_sound::lift_current && !mgs::fulton_sound::foley_current);
    returned = false;
    mgs::fulton_sound::play(memory, context, work + 0x30u, [&](auto &) { returned = true; }, 0x273u);
    assert(returned && guest_calls.empty()); // No stale stage bank may play.
    // PO 1.02 SHDS entries: 0x272 ATRAC3plus, 0x273 ADPCM effect.
    const auto shds272 = mgs::fulton_sound::kShds + 0x34u + 0x272u * 8u;
    memory.words[mgs::fulton_sound::kShds] = 0x53444853u;
    memory.words[shds272] = 0x40010105u;
    memory.words[shds272 + 4] = 0x0100u;
    memory.words[shds272 + 8] = 0x40010103u;
    memory.words[shds272 + 12] = 0;
    mgs::fulton_sound::retype_lift(memory);
    assert(memory.load32(shds272) == 0x40010103u && memory.load32(shds272 + 4) == 0);
    std::vector<std::uint8_t> sound_bank(0x800);
    sound_bank[0] = 'S'; sound_bank[1] = 'P'; sound_bank[2] = 0x32; sound_bank[5] = 8;
    sound_bank[0x7fc] = 'E'; sound_bank[0x7fd] = 'P'; sound_bank[0x7fe] = 0x32;
    assert(mgs::fulton_sound::valid_bank(sound_bank, 0x32, 0x11000));
    assert(!mgs::fulton_sound::valid_bank(sound_bank, 0x31, 0x8000));
    sound_bank[4] = 1;
    assert(!mgs::fulton_sound::valid_bank(sound_bank, 0x32, 0x11000)); // Declared size differs.
    if (argc > 1) {
        Memory captured;
        std::ifstream input(argv[1], std::ios::binary);
        assert(input);
        captured.ram.assign(std::istreambuf_iterator<char>(input), {});
        assert(captured.ram.size() == 32u * 1024u * 1024u);
        const auto local_player = captured.load32(engine + 0x2f7060u);
        const auto selected = nearest(captured, local_player);
        assert(selected.work != 0 && eligible(captured, selected));
        // The saved st01t1 delivery includes the linked stage, not just its
        // unrelocated PRX. Check the exact bytes the runtime guard will read.
        assert(matches(captured, 0x08bb5e8cu, notify_shape));
        assert(matches(captured, 0x08be0048u, cleanup_shape));
        std::cout << "Saved-RAM target selection passed: 0x" << std::hex << selected.name << '\n';
    }
    std::cout << "Fulton selection, capacity, commit identity, stage matching and motion hand-back passed\n";
}
