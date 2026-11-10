// Verify all instruction entries in a compiled module, without running a game.
// c++ -std=c++20 -Iportablekit/include scripts/check-module-coverage.cpp -ldl -o out/check-module-coverage
// out/check-module-coverage LIBRARY.so modBASE_NAME_HASH.bin
#include "psprecomp/corpus_abi.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace psprecomp;
static std::set<std::uint32_t> entries;
struct Host : CorpusRuntime {
    Host(const CorpusHostApi *api, const CorpusProcessState *process) : CorpusRuntime(api, process) {}
};

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const std::filesystem::path image = argv[2];
    const auto base = std::stoul(image.filename().string().substr(3, 8), nullptr, 16);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) throw std::runtime_error(dlerror());
    const auto install = reinterpret_cast<void (*)(CorpusRuntime &)>(dlsym(library, "portablekit_register_overlay"));
    if (!install) throw std::runtime_error("Missing corpus registration");
    CorpusHostApi api{};
    api.register_function = [](auto &, std::uint32_t pc, auto, const char *) { entries.insert(pc); };
    api.register_unit = [](auto &, std::uint32_t, std::uint32_t, std::uint32_t, auto, auto) {};
    CorpusProcessState process{};
    Host host(&api, &process);
    install(host);
    std::set<std::uint32_t> excluded;
    auto sidecar = image;
    sidecar.replace_extension(".stubs");
    std::ifstream stubs(sidecar);
    for (std::string range; stubs >> range;) {
        const auto dash = range.find('-');
        const auto start = std::stoul(range.substr(0, dash), nullptr, 16);
        const auto end = std::stoul(range.substr(dash + 1), nullptr, 16);
        for (auto offset = start; offset < end; offset += 4) excluded.insert(base + offset);
    }
    const auto size = std::filesystem::file_size(image);
    for (std::uint32_t offset = 0; offset + 4 <= size; offset += 4) {
        const auto pc = base + offset;
        if (entries.contains(pc) == excluded.contains(pc)) {
            std::cerr << "Wrong coverage at 0x" << std::hex << pc << '\n';
            return 1;
        }
    }
    std::cout << image.filename().string() << ": " << entries.size() << " native entries, import stubs excluded\n";
    dlclose(library);
}
