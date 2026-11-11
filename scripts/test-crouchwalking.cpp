// Converted loop validation, independent import and default-off persistence.
// Optional args: converted-folder PSP.iso MC-installation.
#include "profile.hpp"
#include "../profiles/po/host/crouchwalking.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>

int main(int argc, char **argv) {
    namespace fs = std::filesystem;
    namespace feature = mgs::crouchwalking;
    const auto root = fs::temp_directory_path() /
        ("crouchwalk-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    struct Cleanup { fs::path path; ~Cleanup() { fs::remove_all(path); } } cleanup{root};
    setenv(portablekit::env_name("DATA_DIR").c_str(), root.c_str(), 1);
    feature::refresh();
    assert(!feature::available && !feature::selected && !feature::enabled);
    assert(!feature::valid({}));
    if (argc > 1) {
        assert(feature::import(argv[1]).empty());
        feature::refresh();
        assert(feature::available && !feature::selected && !feature::enabled);
        const auto installed = feature::read(feature::directory() / "crouchwalk.mtar");
        auto corrupt = installed;
        mgs::fulton_convert::put(corrupt, 188, 0xffffffffu);
        assert(!feature::valid(corrupt));
        corrupt = installed;
        mgs::fulton_convert::put(corrupt, mgs::fulton_convert::get(corrupt, 12) + 4, 0x12436);
        assert(!feature::valid(corrupt));
        assert(!feature::import(root / "missing").empty());
        assert(feature::read(feature::directory() / "crouchwalk.mtar") == installed);
        for (int i = 2; i < argc; ++i) {
            assert(feature::import(argv[i]).empty());
            feature::refresh();
            assert(feature::available && !feature::selected && !feature::enabled);
        }
        feature::save(true);
        assert(feature::selected);
        auto entries = portablekit::install::read_settings_file(root);
        assert(entries.at("pw.crouchwalking") == "1" && !entries.contains("pw.fulton"));
        feature::save(false);
        assert(portablekit::install::read_settings_file(root).at("pw.crouchwalking") == "0");
        fs::remove(feature::directory() / "crouchwalk.mtar");
        feature::refresh();
        assert(!feature::available);
    }
    std::cout << "Crouchwalk validation, import, default-off and selection checks passed\n";
}
