#pragma once

// Reads resources out of Peace Walker: a PSP disc image (checked against
// ULES-01372 1.01) or a Master Collection installation (checked against Steam
// build 25294658). Both hold the same two containers.
//
// SLOT (PSP_GAME/USRDIR/SLOT.KEY and SLOT.DAT; MLG/disc0_rel/002aba34.KEY and
// .DAT). The key file is a 12-byte header and records whose first two words
// are a block's start and end in their low 20 bits, as pages of the data
// file. Each block is enciphered a word at a time, `word ^= acc; acc = acc *
// 0x02E90EDD + low`, restarting at every block from a seed in the header, and
// holds {0x00100004, 0, compressed size, size} followed by a zlib stream.
// Inflated, a block is a count, that many {key, offset} tags and the
// payloads; a resource runs from its offset to the next tag's. Keys are
// `type << 24 | name hash`. A type 0x7F tag with a non-zero name moves the
// payload base up to the next page.
//
// The stage container (STAGEDAT.PDT; 009645fa.PDT) uses the same word cipher,
// seeded from its first 12 bytes. One stream runs over the header that
// follows ({?, 0x007A7E9C, version, u16 entity count, ...}) and on into the
// table of {size, hash, offset} entities; each entity restarts the stream and
// holds its size followed by a zlib stream. Most inflate to a file archive: a
// count, then per file a zero-terminated name, its length on the next 4-byte
// boundary and its bytes on the 16-byte boundary after that.
//
//                      PSP             Master Collection
//   page               2048            4096
//   key record         12 bytes        20 bytes
//   tag                two u32         two u64 (the count too)
//   stage header       32 bytes        40 bytes
//
// The Master Collection also wraps every file in a keystream of its own:
// MT19937 seeded from a hash of the file's name, `word ^= next ^ 0xB9D3018F`
// from the generator's sixth word on. It runs once over a key file and over a
// stage container's header and table, and restarts at every SLOT block and
// stage entity.
#include "kernel/iso_image.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#define STBI_ONLY_PNG
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace mgs::pw_slot {
inline std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t value;
    if (at + 4 > bytes.size()) throw std::runtime_error("PW data is truncated");
    std::memcpy(&value, bytes.data() + at, 4);
    return value;
}

// The word cipher both containers use, seeded from three header words.
struct Cipher {
    std::uint32_t high, low;
    explicit Cipher(std::span<const std::uint8_t> header) {
        const std::uint32_t h0 = word(header, 0) ^ word(header, 4);
        high = h0 | ((h0 ^ 0x6576u) << 16);
        low = h0 * word(header, 8);
    }
    // Deciphers whole words in place and returns the state to continue from.
    std::uint32_t apply(std::span<std::uint8_t> bytes, std::uint32_t acc) const {
        for (std::size_t at = 0; at + 4 <= bytes.size(); at += 4, acc = acc * 0x02e90eddu + low) {
            const std::uint32_t plain = word(bytes, at) ^ acc;
            std::memcpy(bytes.data() + at, &plain, 4);
        }
        return acc;
    }
};

// The Master Collection's keystream for a file of this name. The name hash
// stops at the extension; the generator is filled the 1998 way, two steps of
// `s = s * 69069 + 1` a word, and its first five words are discarded.
class Keystream {
public:
    explicit Keystream(std::string_view name) {
        std::uint32_t seed = 0;
        for (char c : name) {
            if (c == '.') break;
            seed = seed * 0x2356fu + std::uint32_t(std::int32_t(static_cast<signed char>(c))) * 0x1d35u;
        }
        for (auto &state : mt_) {
            state = seed & 0xffff0000u;
            seed = seed * 69069u + 1u;
            state |= (seed & 0xffff0000u) >> 16;
            seed = seed * 69069u + 1u;
        }
        for (int skipped = 0; skipped < 5; ++skipped) next();
    }
    // Removes the layer in place; a ragged tail takes the low bytes of one more word.
    void apply(std::span<std::uint8_t> bytes) {
        std::size_t at = 0;
        for (; at + 4 <= bytes.size(); at += 4) {
            const std::uint32_t plain = word(bytes, at) ^ next() ^ 0xb9d3018fu;
            std::memcpy(bytes.data() + at, &plain, 4);
        }
        if (at == bytes.size()) return;
        const std::uint32_t last = next() ^ 0xb9d3018fu;
        for (unsigned shift = 0; at < bytes.size(); ++at, shift += 8) bytes[at] ^= std::uint8_t(last >> shift);
    }

private:
    std::uint32_t next() {
        if (index_ == 624) {
            for (int k = 0; k < 624; ++k) {
                const std::uint32_t y = (mt_[k] & 0x80000000u) | (mt_[(k + 1) % 624] & 0x7fffffffu);
                mt_[k] = mt_[(k + 397) % 624] ^ (y >> 1) ^ ((y & 1u) ? 0x9908b0dfu : 0u);
            }
            index_ = 0;
        }
        std::uint32_t y = mt_[index_++];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        return y ^ (y >> 18);
    }
    std::uint32_t mt_[624];
    int index_ = 624;
};

inline bool inflate(std::span<const std::uint8_t> packed, std::vector<std::uint8_t> &out, std::uint32_t size) {
    if (size > 64u << 20 || packed.size() > 64u << 20) return false;
    out.resize(size);
    return stbi_zlib_decode_buffer(reinterpret_cast<char *>(out.data()), int(size),
                                   reinterpret_cast<const char *>(packed.data()), int(packed.size())) == int(size);
}

// One container file: read-only bytes at an offset, wherever they live.
struct File {
    std::function<std::size_t(std::uint64_t, std::span<std::uint8_t>)> read;
    std::uint64_t size = 0;
    std::string name; // what the Master Collection keys its keystream on

    std::vector<std::uint8_t> bytes(std::uint64_t offset, std::uint64_t length, const char *what) const {
        if (length > 64u << 20 || offset + length > size) throw std::runtime_error(std::string("Invalid ") + what);
        std::vector<std::uint8_t> out(length);
        if (read(offset, out) != out.size()) throw std::runtime_error(std::string("Cannot read ") + what);
        return out;
    }
};

// A Peace Walker source and the widths its edition uses.
struct Source {
    File key, data, stage;
    bool wide = false; // Master Collection: 64-bit fields and the outer keystream
    std::uint64_t page() const { return wide ? 4096 : 2048; }
};

inline Source psp(portablekit::IsoImage &iso) {
    Source source;
    const auto open = [&iso](const char *path) {
        const auto entry = iso.find(path);
        if (!entry) throw std::runtime_error("Not a Peace Walker PSP disc image");
        const std::uint64_t origin = std::uint64_t(entry->lba) * portablekit::IsoImage::kSectorSize;
        return File{[&iso, origin](std::uint64_t at, std::span<std::uint8_t> out) { return iso.read(origin + at, out); },
                    entry->size, {}};
    };
    source.key = open("PSP_GAME/USRDIR/SLOT.KEY");
    source.data = open("PSP_GAME/USRDIR/SLOT.DAT");
    source.stage = open("PSP_GAME/USRDIR/STAGEDAT.PDT");
    return source;
}

// `folder` is the installation, or anything above its MLG/disc0_rel.
inline Source master_collection(std::filesystem::path folder) {
    namespace fs = std::filesystem;
    std::error_code error;
    for (const char *inner : {"MLG/disc0_rel", "disc0_rel", "mgspw/MLG/disc0_rel"})
        if (fs::is_regular_file(folder / inner / "002aba34.KEY", error)) { folder /= inner; break; }
    Source source;
    source.wide = true;
    const auto open = [&folder](const char *name) {
        auto stream = std::make_shared<std::ifstream>(folder / name, std::ios::binary);
        std::error_code failed;
        const auto size = fs::file_size(folder / name, failed);
        if (!*stream || failed) throw std::runtime_error("Not a Peace Walker Master Collection installation");
        return File{[stream](std::uint64_t at, std::span<std::uint8_t> out) {
                        stream->clear();
                        stream->seekg(std::streamoff(at));
                        stream->read(reinterpret_cast<char *>(out.data()), std::streamsize(out.size()));
                        return std::size_t(stream->gcount());
                    },
                    size, name};
    };
    source.key = open("002aba34.KEY");
    source.data = open("002aba34.DAT");
    source.stage = open("009645fa.PDT");
    return source;
}

// The first copy of each wanted key, in container order. Keys the source
// lacks are absent from the result. Throws when the SLOT container is not
// readable as this edition's.
inline std::map<std::uint32_t, std::vector<std::uint8_t>> extract(const Source &source,
                                                                  std::span<const std::uint32_t> wanted) {
    const std::uint64_t page = source.page(), record = source.wide ? 20 : 12, field = source.wide ? 8 : 4;
    if (source.key.size < 12 + record || source.key.size > 1u << 20 || (source.key.size - 12) % record)
        throw std::runtime_error("Invalid SLOT key file");
    auto key = source.key.bytes(0, source.key.size, "SLOT key file");
    if (source.wide) Keystream(source.key.name).apply(key);
    const Cipher cipher(key);

    std::map<std::uint32_t, std::vector<std::uint8_t>> found;
    std::vector<std::uint8_t> data;
    for (std::size_t at = 12; at < key.size() && found.size() < wanted.size(); at += record) {
        const std::uint64_t start = word(key, at) & 0xfffffu, end = word(key, at + 4) & 0xfffffu;
        if (!start || end <= start || end - start > 0xfffu || end * page > source.data.size)
            throw std::runtime_error("Invalid SLOT key record");
        auto block = source.data.bytes(start * page, (end - start) * page, "SLOT block");
        if (source.wide) Keystream(source.data.name).apply(block);
        cipher.apply(block, cipher.high);
        const std::uint32_t compressed = word(block, 8);
        if (word(block, 0) != 0x00100004u || compressed > block.size() - 16)
            throw std::runtime_error("Invalid SLOT block");
        if (!inflate(std::span(block).subspan(16, compressed), data, word(block, 12)))
            throw std::runtime_error("SLOT block does not inflate");
        const std::uint64_t count = word(data, 0);
        std::uint64_t base = field + count * 2 * field;
        if (!count || count > 100000 || base > data.size()) throw std::runtime_error("Invalid SLOT block tags");
        for (std::uint64_t i = 0; i + 1 < count; ++i) {
            const std::uint64_t tag = field + i * 2 * field;
            const std::uint32_t code = word(data, tag), offset = word(data, tag + field);
            if (code >> 24 == 0x7fu) {
                if (code & 0xffffffu) base = (base + page - 1) & ~(page - 1);
                continue;
            }
            if (std::find(wanted.begin(), wanted.end(), code) == wanted.end() || found.contains(code)) continue;
            const std::uint64_t next = word(data, tag + 3 * field);
            if (offset > next || base + next > data.size()) throw std::runtime_error("Invalid SLOT resource span");
            found[code].assign(data.begin() + base + offset, data.begin() + base + next);
        }
    }
    return found;
}

// Calls visit(name, bytes) for every file of every stage entity, in container
// order, until it returns true. Entities that are not file archives are
// passed over. Throws when the stage container is not readable.
template<class Visit>
void stage_files(const Source &source, Visit visit) {
    const File &file = source.stage;
    const std::uint64_t table_at = source.wide ? 40 : 32;
    if (file.size < table_at + 12) throw std::runtime_error("Invalid stage container");
    // The count sits in the enciphered header, so the header comes first; the
    // Master Collection's keystream then has to run over header and table at once.
    const auto opened = [&](std::uint64_t length) {
        auto bytes = file.bytes(0, length, "stage container");
        if (source.wide) Keystream(file.name).apply(bytes);
        return bytes;
    };
    auto head = opened(table_at);
    const Cipher cipher(head);
    cipher.apply(std::span(head).subspan(12), cipher.high);
    const std::uint32_t count = word(head, 24) & 0xffffu;
    if (word(head, 16) != 0x007a7e9cu || !count || table_at + count * 12ull > file.size)
        throw std::runtime_error("Invalid stage container header");
    auto table = opened(table_at + count * 12ull);
    cipher.apply(std::span(table).subspan(12), cipher.high);
    std::vector<std::uint8_t> data;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint64_t at = table_at + index * 12ull, size = word(table, at), offset = word(table, at + 8);
        if (size < 8 || !offset || offset + size > file.size) throw std::runtime_error("Invalid stage entity");
        if (size > 64u << 20) continue; // No file archive is that large.
        auto entity = file.bytes(offset, size, "stage entity");
        if (source.wide) Keystream(file.name).apply(entity);
        cipher.apply(entity, cipher.high);
        if (!inflate(std::span(entity).subspan(4), data, word(entity, 0)) || data.size() < 8) continue;
        const std::uint32_t files = word(data, 0);
        std::size_t position = 4;
        for (std::uint32_t i = 0; i < files && files < 10000; ++i) {
            const auto end = std::find(data.begin() + position, data.end(), 0) - data.begin();
            const std::size_t length_at = (end + 4) & ~std::size_t(3);
            if (end == std::ptrdiff_t(position) || end - position > 200 || length_at + 4 > data.size()) break;
            const std::size_t length = word(data, length_at), start = (length_at + 19) & ~std::size_t(15);
            if (start + length > data.size()) break;
            const std::string_view name(reinterpret_cast<const char *>(data.data()) + position, end - position);
            if (visit(name, std::span<const std::uint8_t>(data).subspan(start, length))) return;
            position = start + length + 1;
        }
    }
}
} // namespace mgs::pw_slot
