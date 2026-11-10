#pragma once

// Reads resources out of a Peace Walker PSP disc image: the SLOT container
// (PSP_GAME/USRDIR/SLOT.KEY and SLOT.DAT). Checked against ULES-01372 1.01.
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
#include "kernel/iso_image.hpp"

#include <cstring>
#include <map>
#include <span>
#include <stdexcept>
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
    const std::uint32_t h0 = word(key, 0) ^ word(key, 4);
    const std::uint32_t high = h0 | ((h0 ^ 0x6576u) << 16), low = h0 * word(key, 8);

    std::map<std::uint32_t, std::vector<std::uint8_t>> found;
    std::vector<std::uint8_t> block, data;
    for (std::size_t record = 12; record < key.size() && found.size() < wanted.size(); record += 12) {
        const std::uint64_t start = word(key, record) & 0xfffffu, end = word(key, record + 4) & 0xfffffu;
        if (!start || end <= start || end - start > 0xfffu || end * page > dat_file->size)
            throw std::runtime_error("Invalid SLOT.KEY record");
        block.resize((end - start) * page);
        if (iso.read(dat_file->lba * page + start * page, block) != block.size())
            throw std::runtime_error("Cannot read SLOT.DAT");
        std::uint32_t acc = high;
        for (std::size_t at = 0; at + 4 <= block.size(); at += 4, acc = acc * 0x02e90eddu + low) {
            const std::uint32_t plain = word(block, at) ^ acc;
            std::memcpy(block.data() + at, &plain, 4);
        }
        const std::uint32_t compressed = word(block, 8), size = word(block, 12);
        if (word(block, 0) != 0x00100004u || compressed > block.size() - 16 || size > 64u << 20)
            throw std::runtime_error("Invalid SLOT.DAT block");
        data.resize(size);
        if (stbi_zlib_decode_buffer(reinterpret_cast<char *>(data.data()), int(size),
                                    reinterpret_cast<const char *>(block.data()) + 16, int(compressed)) != int(size))
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
} // namespace mgs::pw_slot
