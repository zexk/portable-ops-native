#pragma once

#include "hle/hle_common.hpp"
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
#include <vector>

namespace mgs::fulton_assets {
inline bool enabled() {
    const char *fulton = portablekit::hle_extension::env("FULTON");
    const char *visuals = portablekit::hle_extension::env("FULTON_VISUALS");
    return fulton && visuals && std::strcmp(fulton, "1") == 0 && std::strcmp(visuals, "1") == 0;
}

struct Stage {
    std::string original_hash, prefix, replacement_hash;
    std::size_t size;
    std::filesystem::path replacement;
};
inline std::vector<Stage> stages;

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
        std::ifstream file(stage.replacement, std::ios::binary);
        if (!file) return;
        std::vector<std::uint8_t> replacement{std::istreambuf_iterator<char>(file), {}};
        if (replacement.size() != stage.size || psprecomp::sha256_bytes(replacement) != stage.replacement_hash) {
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

inline void install(portablekit::HleRegistrar &hle) {
    if (!enabled()) return;
    const char *directory = portablekit::hle_extension::env("FULTON_STAGE_DIR");
    if (!directory) return;
    const std::filesystem::path root(directory);
    std::ifstream index(root / "index.txt");
    std::string line;
    while (std::getline(index, line)) {
        Stage stage;
        std::string filename;
        std::istringstream row(line);
        if (!(row >> stage.original_hash >> stage.size >> stage.prefix >> stage.replacement_hash >> filename) ||
            stage.size < 64 || stage.size > 32u * 1024u * 1024u || stage.prefix.size() != 128 ||
            stage.original_hash.size() != 64 || stage.replacement_hash.size() != 64 ||
            filename != std::filesystem::path(filename).filename().string()) continue;
        stage.replacement = root / filename;
        stages.push_back(std::move(stage));
    }
    if (stages.empty()) { std::cerr << "[fulton] no verified donor stages configured" << std::endl; return; }
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
