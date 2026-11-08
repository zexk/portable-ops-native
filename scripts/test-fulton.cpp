// No game or window required. Exercise selection and capture commit rules.
#include "../profiles/po/host/fulton.hpp"
#include <cassert>
#include <fstream>
#include <map>
#include <vector>

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
};

int main(int argc, char **argv) {
    using namespace mgs::fulton;
    assert((0x1c00u & chord) == chord); // Observed engine L + R + Triangle.
    assert((0x1300u & chord) != chord); // Raw sceCtrl mask is not the engine mask.
    Memory memory;
    constexpr std::uint32_t player = 0x08a50000u, work = 0x09b10000u, component = 0x09b20000u;
    constexpr Target target{work, 0x15f7f777u};
    memory.words[work] = target.name;
    memory.words[work + 0x10] = component;
    memory.words[work + 0x1438] = 79;
    memory.words[work + 0x1850] = 23;
    memory.words[work + 0x1858] = 0x804;
    memory.words[component] = 0x1002;
    memory.words[component + 4] = target.name;
    memory.words[component + 0x24] = work;
    assert(eligible(memory, target));
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
    memory.words[work + 0x1438] = 88;
    assert(!eligible(memory, target)); // Unsupported scenario callback.
    memory.words[work + 0x1438] = 79;

    constexpr std::uint32_t node = 0x09b30000u;
    memory.words[engine + 0x258f8c] = node;
    memory.words[node + 0x10] = component;
    memory.words[work + 0x30] = std::bit_cast<std::uint32_t>(1000.0f);
    assert(nearest(memory, player).work == work);
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
    std::cout << "Fulton selection, capacity, commit identity and stage matching passed\n";
}
