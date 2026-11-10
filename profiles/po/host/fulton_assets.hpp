#pragma once

#include "hle/hle_common.hpp"
#include "install/user_data.hpp"
#include "portablekit/hle_extension.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

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

struct Stage {
    std::string original_hash, prefix, replacement_hash;
    std::size_t size;
    std::filesystem::path replacement;
};
inline std::vector<Stage> stages;

inline std::vector<std::uint8_t> read_stage(const Stage &stage) {
    std::ifstream file(stage.replacement, std::ios::binary);
    std::error_code error;
    if (!file || std::filesystem::is_symlink(stage.replacement, error) || error) return {};
    std::vector<std::uint8_t> bytes(stage.size);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), bytes.size()) || file.peek() != EOF ||
        psprecomp::sha256_bytes(bytes) != stage.replacement_hash) return {};
    return bytes;
}

inline std::vector<Stage> checked_stages(const std::filesystem::path &root) {
    const auto index_path = root / "index.txt";
    if (std::filesystem::is_symlink(index_path) || std::filesystem::file_size(index_path) > 256u * 1024u)
        throw std::runtime_error("Invalid Fulton stage index");
    std::ifstream index(index_path);
    if (!index) throw std::runtime_error("Cannot read Fulton stage index");
    const auto hex = [](const std::string &value, std::size_t size) {
        return value.size() == size && value.find_first_not_of("0123456789abcdef") == std::string::npos;
    };
    std::vector<Stage> result;
    std::string line;
    std::size_t total = 0;
    while (std::getline(index, line)) {
        Stage stage;
        std::string filename, extra;
        std::istringstream row(line);
        if (result.size() == 256 ||
            !(row >> stage.original_hash >> stage.size >> stage.prefix >> stage.replacement_hash >> filename) ||
            (row >> extra) || !hex(stage.original_hash, 64) || !hex(stage.replacement_hash, 64) ||
            !hex(stage.prefix, 128) || stage.size < 64 || stage.size > 32u * 1024u * 1024u ||
            filename.find_first_of("/\\:") != std::string::npos ||
            filename.find('\0') != std::string::npos ||
            !filename.ends_with(".zar") || filename == ".zar")
            throw std::runtime_error("Invalid Fulton stage index row");
        stage.replacement = root / filename;
        total += stage.size;
        if (std::any_of(result.begin(), result.end(), [&](const Stage &other) {
                return other.replacement == stage.replacement;
            }) || std::filesystem::is_symlink(stage.replacement) ||
            std::filesystem::file_size(stage.replacement) != stage.size ||
            total > 512u * 1024u * 1024u || read_stage(stage).empty())
            throw std::runtime_error("Fulton stage failed size/hash check: " + filename);
        result.push_back(std::move(stage));
    }
    if (index.bad() || result.empty()) throw std::runtime_error("No readable converted Fulton stages installed");
    return result;
}

class Filter final : public portablekit::hle_extension::FileFilter {
public:
    explicit Filter(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    std::uint64_t size() const override { return bytes_.size(); }
    std::size_t read(std::uint64_t offset, std::uint8_t *destination, std::size_t length) override {
        if (offset >= bytes_.size()) return 0;
        length = std::min(length, bytes_.size() - static_cast<std::size_t>(offset));
        std::memcpy(destination, bytes_.data() + offset, length);
        return length;
    }
private:
    std::vector<std::uint8_t> bytes_;
};
inline std::map<std::uint32_t, std::shared_ptr<Filter>> installed;

inline void before_read(std::uint32_t fd) {
    if (installed.contains(fd)) return;
    std::array<std::uint8_t, 64> prefix{};
    if (portablekit::hle_extension::read_open_file(fd, 0, prefix) != prefix.size()) return;
    constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (auto byte : prefix) { hex += digits[byte >> 4]; hex += digits[byte & 15]; }
    for (const auto &stage : stages) {
        if (stage.prefix != hex) continue;
        std::vector<std::uint8_t> original(stage.size);
        if (portablekit::hle_extension::read_open_file(fd, 0, original) != original.size() ||
            psprecomp::sha256_bytes(original) != stage.original_hash) return;
        auto replacement = read_stage(stage);
        if (replacement.empty()) {
            std::cerr << "[fulton] donor stage failed size/hash check" << std::endl;
            return;
        }
        auto filter = std::make_shared<Filter>(std::move(replacement));
        if (portablekit::hle_extension::set_file_filter(fd, filter)) {
            installed[fd] = std::move(filter);
            std::cout << "[fulton] donor stage supplied: " << stage.replacement.filename().string() << std::endl;
        }
        return;
    }
}

inline void load() {
    // Startup snapshot: changing the selection or importing assets needs a
    // restart. No UI thread changes a live stage filter or guest hook.
    try {
        selected = selection(portablekit::install::read_settings_file(portablekit::install::user_data_directory()));
        stages = checked_stages(directory());
        available = true;
        status = std::to_string(stages.size()) + " prepared stages";
    } catch (const std::exception &error) {
        stages.clear();
        available = false;
        status = "Converted Fulton assets missing or invalid";
        std::cerr << "[fulton] inactive: " << error.what() << std::endl;
    }
    const char *fulton = portablekit::hle_extension::env("FULTON");
    const char *visuals = portablekit::hle_extension::env("FULTON_VISUALS");
    const bool requested = fulton ? std::strcmp(fulton, "1") == 0 && visuals && std::strcmp(visuals, "1") == 0 : selected;
    active = available && requested;
}

inline void install(portablekit::HleRegistrar &hle) {
    load();
    if (!active) return;
    // The replacement has the original file's length, so getstat and disc
    // directory sizes remain valid. Attach it before reads or seek ioctls.
    for (auto nid : {0x6a638d83u, 0xa0b5a7c2u, 0x27eb27b8u, 0x68963324u,
                     0x71b19e77u, 0x1b385d8fu, 0x63632449u, 0xe95a012bu}) {
        auto previous = hle.bound_function("IoFileMgrForUser", nid);
        if (!previous) continue;
        hle.bind("IoFileMgrForUser", nid, [previous](auto &runtime, auto &ctx) {
            before_read(ctx.gpr[4]);
            previous(runtime, ctx);
        });
    }
    for (auto nid : {0x810c4bc3u, 0xff5940b6u}) {
        auto previous = hle.bound_function("IoFileMgrForUser", nid);
        if (!previous) continue;
        hle.bind("IoFileMgrForUser", nid, [previous](auto &runtime, auto &ctx) {
            installed.erase(ctx.gpr[4]);
            previous(runtime, ctx);
        });
    }
}
} // namespace mgs::fulton_assets
