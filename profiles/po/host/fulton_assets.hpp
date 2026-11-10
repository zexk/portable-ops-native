#pragma once

#include "hle/hle_common.hpp"
#include "install/user_data.hpp"
#include "portablekit/hle_extension.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// The converted Fulton bundle: PW donors already in PO's formats, as plain
// files in the port's user data. Nothing here reads a PW source, and the
// game's own disc files are never replaced; fulton_visuals hands the donors
// to PO's resource dispatcher from memory in whatever stage is loaded.
namespace mgs::fulton_assets {
inline std::filesystem::path directory() {
    if (const char *override = portablekit::hle_extension::env("FULTON_STAGE_DIR"); override && *override)
        return portablekit::install::path_from_utf8(override);
    return portablekit::install::user_data_directory() / "pw" / "fulton";
}

inline bool selected = false, available = false, active = false;
inline std::string status;
inline bool enabled() { return active; }

inline bool selection(const portablekit::install::SettingsEntries &entries) {
    const auto found = entries.find("pw.fulton");
    return found != entries.end() && found->second == "1";
}

struct File {
    const char *name;
    std::uint32_t limit;                    // largest size taken
    bool required;
    std::array<std::uint32_t, 3> header;    // leading words; a zero is not compared
};
// Models and packs are PO-layout (.txp header {0, 1, ...}); the lift archive
// is PO Mtar version 19 with its two clips. The sound banks and the placement
// archive are optional and checked again by the code that files them.
constexpr File files[] = {
    {"fult_item_balloon.txp", 0x10000u, true, {0u, 1u, 0u}},
    {"fult_item_body.txp", 0x10000u, true, {0u, 1u, 0u}},
    {"fult_item_balloon.mdp", 0x10000u, true, {0x2050444du, 0u, 0u}},
    {"fult_item_body.mdp", 0x10000u, true, {0x2050444du, 0u, 0u}},
    {"fult_enemy.mtar", 0x10000u, true, {0x7261744du, 19u, 2u}},
    {"fult_place.mtar", 0x10000u, false, {0x7261744du, 19u, 1u}},
    {"fulton.sep", 0x8000u, false, {}},
    {"fulton-lift.sep", 0x11000u, false, {}},
    {"fulton-foley.sep", 0x9000u, false, {}},
};

// One bundle file, or empty when an optional one is absent. Throws on a file
// that is present but not what its name says.
inline std::vector<std::uint8_t> read(const std::filesystem::path &root, const File &file) {
    const auto path = root / file.name;
    std::error_code error;
    const bool exists = std::filesystem::is_regular_file(path, error) && !std::filesystem::is_symlink(path, error);
    if (!exists && !file.required) return {};
    std::ifstream input(path, std::ios::binary);
    std::vector<std::uint8_t> bytes(exists ? file.limit + 1u : 0u);
    input.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()));
    bytes.resize(std::size_t(input.gcount()));
    std::array<std::uint32_t, 3> header{};
    if (bytes.size() >= 32) std::memcpy(header.data(), bytes.data(), sizeof(header));
    bool valid = exists && bytes.size() >= 32 && bytes.size() <= file.limit;
    for (std::size_t i = 0; i < header.size(); ++i)
        valid = valid && (!file.header[i] || header[i] == file.header[i]);
    if (!valid) throw std::runtime_error(std::string("Missing or invalid Fulton asset: ") + file.name);
    return bytes;
}

// Throws unless every required file is present and valid.
inline void check(const std::filesystem::path &root) {
    for (const auto &file : files) read(root, file);
}

// Copies a converted bundle into the port's user data: the named files and
// nothing else. The copy is checked before it replaces anything. Selection
// and the running game are untouched.
inline void import_bundle(const std::filesystem::path &source) {
    namespace fs = std::filesystem;
    const auto destination = portablekit::install::user_data_directory() / "pw" / "fulton";
    std::error_code ignored;
    if (fs::equivalent(source, destination, ignored)) throw std::runtime_error("Folder is already the installed one");
    const auto staged = destination.parent_path() / ".fulton-import";
    fs::remove_all(staged);
    fs::create_directories(staged);
    try {
        for (const auto &file : files) {
            const auto bytes = read(source, file);
            if (bytes.empty()) continue;
            std::ofstream output(staged / file.name, std::ios::binary);
            output.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
            if (!output.flush()) throw std::runtime_error(std::string("Cannot write ") + file.name);
        }
        check(staged);
        fs::remove_all(destination);
        fs::rename(staged, destination);
    } catch (...) {
        fs::remove_all(staged, ignored);
        throw;
    }
}

inline void load() {
    // Startup snapshot: changing the selection or importing assets needs a
    // restart. No UI thread changes a live guest hook.
    try {
        selected = selection(portablekit::install::read_settings_file(portablekit::install::user_data_directory()));
        check(directory());
        available = true;
        status = "Assets installed";
    } catch (const std::exception &error) {
        available = false;
        status = "Converted Fulton assets missing or invalid";
        std::cerr << "[fulton] inactive: " << error.what() << std::endl;
    }
    const char *fulton = portablekit::hle_extension::env("FULTON");
    const char *visuals = portablekit::hle_extension::env("FULTON_VISUALS");
    const bool requested = fulton ? std::strcmp(fulton, "1") == 0 && visuals && std::strcmp(visuals, "1") == 0 : selected;
    active = available && requested;
}

inline void install(portablekit::HleRegistrar &) { load(); }
} // namespace mgs::fulton_assets
