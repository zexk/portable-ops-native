// No game or renderer: stage integrity, default-off selection and persistence.
#include "profile.hpp"
#include "../profiles/po/host/fulton_settings.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>

namespace portablekit::hle_extension {
const char *env(const char *name) { return portablekit::env(name); }
}

int main(int argc, char **argv) {
    namespace fs = std::filesystem;
    namespace assets = mgs::fulton_assets;
    namespace settings = portablekit::install;
    const auto root = fs::temp_directory_path() /
        ("fulton-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    struct Cleanup { fs::path path; ~Cleanup() { fs::remove_all(path); } } cleanup{root};
    const auto env = [](const char *key, const char *value) {
        const auto name = portablekit::env_name(key);
        if (value) setenv(name.c_str(), value, 1);
        else unsetenv(name.c_str());
    };
    env("DATA_DIR", root.c_str());
    env("FULTON", nullptr);
    env("FULTON_VISUALS", nullptr);
    env("FULTON_STAGE_DIR", nullptr);
    assets::load();
    assert(!assets::selected && !assets::available && !assets::enabled());
    const auto bundle = root / "pw/fulton";
    assert(assets::directory() == bundle);
    fs::create_directories(bundle);
    const std::vector<std::uint8_t> bytes(64, 0x42);
    std::ofstream stage(bundle / "st01.zar", std::ios::binary);
    stage.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    stage.close();
    const auto row = std::string(64, 'a') + " 64 " + std::string(128, 'b') + " " +
                     psprecomp::sha256_bytes(bytes) + " st01.zar\n";
    const auto index = [&](const std::string &text) { std::ofstream(bundle / "index.txt") << text; };
    index(row);
    assets::load();
    assert(assets::available && !assets::enabled() && !assets::selected);
    assert(!fs::exists(root / "settings.ini"));
    settings::write_settings_file(root, {{"disc_image", "keep.iso"}, {"camera.follow", "1"}});
    mgs::fulton_settings::save(true);
    assert(!assets::enabled()); // Selection does not change live hooks.
    assets::load();
    assert(assets::selected && assets::enabled());
    env("FULTON", "0");
    assets::load();
    assert(assets::selected && assets::available && !assets::enabled());
    env("FULTON", "1");
    env("FULTON_VISUALS", "1");
    assets::load();
    assert(assets::enabled());
    env("FULTON", nullptr);
    env("FULTON_VISUALS", nullptr);
    auto entries = settings::read_settings_file(root);
    assert(entries.at("disc_image") == "keep.iso" && entries.at("camera.follow") == "1");
    assert(entries.at("pw.fulton") == "1");
    for (const auto &bad : {row + row, row + "bad\n", std::string("invalid\n"),
                           row.substr(0, row.find("st01.zar")) + "../st01.zar\n"}) {
        index(bad);
        assets::load();
        assert(assets::selected && !assets::available && !assets::enabled());
    }
    index(row);
    // Import: a verified copy, extras by name only, nothing enabled by it.
    const auto donor = root / "donor";
    fs::rename(bundle, donor);
    std::ofstream(donor / "fulton.sep") << "bank";
    std::ofstream(donor / "notes.txt") << "stray";
    mgs::fulton_settings::reset();
    assert(assets::import_bundle(donor) == 1);
    assert(fs::exists(bundle / "fulton.sep") && !fs::exists(bundle / "notes.txt"));
    assert(!assets::selection(settings::read_settings_file(root)));
    fs::remove_all(donor); // The source is not needed afterwards.
    assets::load();
    assert(assets::available && !assets::selected && !assets::enabled());
    try { assets::import_bundle(root / "missing"); assert(false); } catch (const std::exception &) {}
    assets::load();
    assert(assets::available); // A failed import leaves the installed copy.
    mgs::fulton_settings::save(true);
    std::ofstream(bundle / "st01.zar", std::ios::app) << "changed";
    assets::load();
    assert(assets::selected && !assets::enabled());
    fs::remove(bundle / "st01.zar");
    assets::load();
    assert(assets::selected && !assets::enabled());
    mgs::fulton_settings::reset();
    assert(!assets::selection(settings::read_settings_file(root)));
    assert(!assets::selection({{"pw.fulton", "true"}}));
    if (argc > 1) assert(!assets::checked_stages(argv[1]).empty());
    std::cout << "Fulton stage integrity, missing assets, default-off selection and persistence passed\n";
}
