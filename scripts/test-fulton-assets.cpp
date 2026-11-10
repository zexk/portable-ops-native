// No game or renderer: bundle checks, import, default-off selection and persistence.
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

    // A donor folder: every required file with the header its name promises.
    const auto donor = root / "donor";
    fs::create_directory(donor);
    const auto write = [](const fs::path &path, std::array<std::uint32_t, 3> header, std::size_t size = 64) {
        std::vector<char> bytes(size, 0x42);
        std::memcpy(bytes.data(), header.data(), sizeof(header));
        std::ofstream(path, std::ios::binary).write(bytes.data(), std::streamsize(bytes.size()));
    };
    for (const auto &file : assets::files)
        if (file.required) write(donor / file.name, file.header);
    write(donor / "fulton.sep", {});
    std::ofstream(donor / "notes.txt") << "stray";
    assets::check(donor);

    // Import: a checked copy of the named files only, nothing enabled by it.
    assets::import_bundle(donor);
    assert(fs::exists(bundle / "fulton.sep") && !fs::exists(bundle / "notes.txt"));
    assert(!fs::exists(root / "settings.ini"));
    fs::rename(donor, root / "moved"); // The source is not needed afterwards.
    assets::load();
    assert(assets::available && !assets::selected && !assets::enabled());
    try { assets::import_bundle(root / "missing"); assert(false); } catch (const std::exception &) {}
    assets::load();
    assert(assets::available); // A failed import leaves the installed copy.

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

    // Damaged or missing assets leave it inactive and keep the selection.
    const auto lift = bundle / "fult_enemy.mtar";
    for (const auto &header : {std::array<std::uint32_t, 3>{0x7261744du, 19u, 1u}, std::array<std::uint32_t, 3>{}}) {
        write(lift, header);
        assets::load();
        assert(assets::selected && !assets::available && !assets::enabled());
    }
    write(lift, {0x7261744du, 19u, 2u}, 0x10001); // Over the size limit.
    assets::load();
    assert(!assets::available);
    write(lift, {0x7261744du, 19u, 2u});
    assets::load();
    assert(assets::enabled());
    write(bundle / "fult_place.mtar", {}); // A bad optional file is not skipped.
    assets::load();
    assert(!assets::available);
    fs::remove(bundle / "fult_place.mtar");
    fs::remove(lift);
    assets::load();
    assert(assets::selected && !assets::enabled());
    mgs::fulton_settings::reset();
    assert(!assets::selection(settings::read_settings_file(root)));
    assert(!assets::selection({{"pw.fulton", "true"}}));
    if (argc > 1) assets::check(argv[1]);
    std::cout << "Fulton bundle checks, import, default-off selection and persistence passed\n";
}
