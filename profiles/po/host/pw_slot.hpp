#pragma once

// Reads resources out of a Peace Walker PSP disc image: the SLOT container
// (PSP_GAME/USRDIR/SLOT.KEY and SLOT.DAT) and the stage container
// (STAGEDAT.PDT). Checked against ULES-01372 1.01.
//
// SLOT.KEY is a 12-byte header and 12-byte records {start, end, hash}; the low
// 20 bits of start and end are 2048-byte pages of SLOT.DAT. Each block is
// enciphered a word at a time, `word ^= acc; acc = acc * 0x02E90EDD + low`,
// restarting at every block from a seed in the header, and holds
// {0x00100004, 0, compressed size, size} followed by a zlib stream. Inflated,
// a block is a count, that many {key, offset} tags and the payloads; a
// resource runs from its offset to the next tag's. Keys are
// `type << 24 | name hash`. A type 0x7F tag with a non-zero name moves the
// payload base up to the next page.
//
// STAGEDAT.PDT uses the same word cipher, seeded from its first 12 bytes. One
// stream runs over the 20-byte header that follows ({?, 0x007A7E9C, version,
// u16 entity count, ...}) and on into the table at 32 of {size, hash, offset}
// entities; each entity restarts the stream and holds its size followed by a
// zlib stream. Most inflate to a file archive: a count, then per file a
// zero-terminated name, its length on the next 4-byte boundary and its bytes
// on the 16-byte boundary after that.
#include "kernel/iso_image.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#define STBI_ONLY_PNG
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace mgs::pw_slot {
inline std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t value;
    if (at + 4 > bytes.size()) throw std::runtime_error("PW slot data is truncated");
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

inline bool inflate(std::span<const std::uint8_t> packed, std::vector<std::uint8_t> &out, std::uint32_t size) {
    if (size > 64u << 20 || packed.size() > 64u << 20) return false;
    out.resize(size);
    return stbi_zlib_decode_buffer(reinterpret_cast<char *>(out.data()), int(size),
                                   reinterpret_cast<const char *>(packed.data()), int(packed.size())) == int(size);
}

// The first copy of each wanted key, in disc order. Keys the disc lacks are
// absent from the result. Throws when the image is not a readable PW disc.
inline std::map<std::uint32_t, std::vector<std::uint8_t>> extract(portablekit::IsoImage &iso,
                                                                  std::span<const std::uint32_t> wanted) {
    constexpr std::uint64_t page = portablekit::IsoImage::kSectorSize;
    const auto key_file = iso.find("PSP_GAME/USRDIR/SLOT.KEY"), dat_file = iso.find("PSP_GAME/USRDIR/SLOT.DAT");
    if (!key_file || !dat_file || key_file->size < 24 || key_file->size > 1u << 20 || (key_file->size - 12) % 12)
        throw std::runtime_error("Not a Peace Walker PSP disc image");
    std::vector<std::uint8_t> key(key_file->size);
    if (iso.read(key_file->lba * page, key) != key.size()) throw std::runtime_error("Cannot read SLOT.KEY");
    const Cipher cipher(key);

    std::map<std::uint32_t, std::vector<std::uint8_t>> found;
    std::vector<std::uint8_t> block, data;
    for (std::size_t record = 12; record < key.size() && found.size() < wanted.size(); record += 12) {
        const std::uint64_t start = word(key, record) & 0xfffffu, end = word(key, record + 4) & 0xfffffu;
        if (!start || end <= start || end - start > 0xfffu || end * page > dat_file->size)
            throw std::runtime_error("Invalid SLOT.KEY record");
        block.resize((end - start) * page);
        if (iso.read(dat_file->lba * page + start * page, block) != block.size())
            throw std::runtime_error("Cannot read SLOT.DAT");
        cipher.apply(block, cipher.high);
        const std::uint32_t compressed = word(block, 8);
        if (word(block, 0) != 0x00100004u || compressed > block.size() - 16)
            throw std::runtime_error("Invalid SLOT.DAT block");
        if (!inflate(std::span(block).subspan(16, compressed), data, word(block, 12)))
            throw std::runtime_error("SLOT.DAT block does not inflate");
        const std::uint64_t count = word(data, 0);
        std::uint64_t base = 4 + count * 8;
        if (!count || base > data.size()) throw std::runtime_error("Invalid SLOT.DAT block tags");
        for (std::uint64_t i = 0; i + 1 < count; ++i) {
            const std::uint32_t code = word(data, 4 + i * 8), offset = word(data, 8 + i * 8);
            if (code >> 24 == 0x7fu) {
                if (code & 0xffffffu) base = (base + page - 1) & ~(page - 1);
                continue;
            }
            bool want = false;
            for (auto each : wanted) want = want || each == code;
            if (!want || found.contains(code)) continue;
            const std::uint64_t next = word(data, 8 + (i + 1) * 8);
            if (offset > next || base + next > data.size()) throw std::runtime_error("Invalid SLOT.DAT resource span");
            found[code].assign(data.begin() + base + offset, data.begin() + base + next);
        }
    }
    return found;
}
// Calls visit(name, bytes) for every file of every stage entity, in disc
// order, until it returns true. Entities that are not file archives are
// passed over. Throws when the image has no readable stage container.
template<class Visit>
void stage_files(portablekit::IsoImage &iso, Visit visit) {
    constexpr std::uint64_t page = portablekit::IsoImage::kSectorSize;
    const auto file = iso.find("PSP_GAME/USRDIR/STAGEDAT.PDT");
    if (!file || file->size < 44) throw std::runtime_error("Not a Peace Walker PSP disc image");
    const std::uint64_t origin = file->lba * page;
    std::vector<std::uint8_t> head(32);
    if (iso.read(origin, head) != head.size()) throw std::runtime_error("Cannot read STAGEDAT.PDT");
    const Cipher cipher(head);
    const auto state = cipher.apply(std::span(head).subspan(12), cipher.high);
    const std::uint32_t count = word(head, 24) & 0xffffu;
    if (word(head, 16) != 0x007a7e9cu || !count || 32 + count * 12ull > file->size)
        throw std::runtime_error("Invalid STAGEDAT.PDT header");
    std::vector<std::uint8_t> table(count * 12u), entity, data;
    if (iso.read(origin + 32, table) != table.size()) throw std::runtime_error("Cannot read STAGEDAT.PDT");
    cipher.apply(table, state);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint64_t size = word(table, index * 12u), offset = word(table, index * 12u + 8);
        if (size < 8 || !offset || offset + size > file->size) throw std::runtime_error("Invalid STAGEDAT.PDT entity");
        entity.resize(size);
        if (iso.read(origin + offset, entity) != entity.size()) throw std::runtime_error("Cannot read STAGEDAT.PDT");
        cipher.apply(entity, cipher.high);
        if (!inflate(std::span(entity).subspan(4), data, word(entity, 0)) || data.size() < 8) continue;
        const std::uint32_t files = word(data, 0);
        std::size_t at = 4;
        for (std::uint32_t i = 0; i < files && files < 10000; ++i) {
            const auto end = std::find(data.begin() + at, data.end(), 0) - data.begin();
            const std::size_t length_at = (end + 4) & ~std::size_t(3);
            if (end == std::ptrdiff_t(at) || end - at > 200 || length_at + 4 > data.size()) break;
            const std::size_t length = word(data, length_at), start = (length_at + 19) & ~std::size_t(15);
            if (start + length > data.size()) break;
            const std::string_view name(reinterpret_cast<const char *>(data.data()) + at, end - at);
            if (visit(name, std::span<const std::uint8_t>(data).subspan(start, length))) return;
            at = start + length + 1;
        }
    }
}
} // namespace mgs::pw_slot
