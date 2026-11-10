#pragma once

// Peace Walker PSP Fulton resources to the port's converted bundle files.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace mgs::fulton_convert {
using Bytes = std::vector<std::uint8_t>;

inline std::uint32_t get(std::span<const std::uint8_t> bytes, std::size_t at, std::size_t width = 4) {
    std::uint32_t value = 0;
    if (at + width > bytes.size()) throw std::runtime_error("PW resource is truncated");
    std::memcpy(&value, bytes.data() + at, width);
    return value;
}
inline void put(Bytes &bytes, std::size_t at, std::uint32_t value) {
    if (at + 4 > bytes.size()) throw std::runtime_error("PW resource is truncated");
    std::memcpy(bytes.data() + at, &value, 4);
}
inline void append(Bytes &bytes, std::initializer_list<std::uint32_t> words) {
    for (auto word : words) { bytes.resize(bytes.size() + 4); put(bytes, bytes.size() - 4, word); }
}

// PW PSP texture pack to PO's layout (the one stage cache.qar files use).
// PW: header {0, name hash, images, textures, ?, image table, texture table,
// data}; image {u16 format, u16 w, u16 h, u16 0, u32 0, u32 pixels, u32 0};
// texture (0x28) {0, hash, image, clut, u, v, su, sv, h << 16 | w, 0}.
// PO: header {0, 1, images, image table, textures, texture table, 0, 0};
// image (0x10) {h << 16 | w, format, pixels, clut}; texture (0x30)
// {0x10, hash, 0, h << 16 | w, 0, image, 0, u, v, su, sv, 0}. Every PO image
// owns its CLUT, so each PW texture gets its own image entry. PO's loader
// crashes on the PW layout (it relocates the pointers twice).
inline Bytes txp(std::span<const std::uint8_t> pw) {
    const std::uint32_t images = get(pw, 8), textures = get(pw, 12), image_table = get(pw, 20),
                        texture_table = get(pw, 24), data = get(pw, 28);
    if (!images || !textures || textures > 256 || image_table < 0x20 || image_table >= texture_table ||
        texture_table >= data || data > pw.size())
        throw std::runtime_error("Not a PW PSP texture pack");
    const std::uint32_t new_data = (0x20u + textures * 0x40u + 15u) & ~15u;
    Bytes out, records;
    append(out, {0, 1, textures, 0x20, textures, 0x20 + textures * 0x10, 0, 0});
    for (std::uint32_t i = 0; i < textures; ++i) {
        const std::size_t texture = texture_table + 0x28u * i;
        const std::uint32_t image = get(pw, texture + 8), clut = get(pw, texture + 12), pixels = get(pw, image + 12);
        if (pixels < data || pixels >= pw.size() || clut < data || clut >= pw.size())
            throw std::runtime_error("PW texture offsets are outside its data");
        append(out, {get(pw, image + 4, 2) << 16 | get(pw, image + 2, 2), get(pw, image, 2),
                     pixels - data + new_data, clut - data + new_data});
        append(records, {0x10, get(pw, texture + 4), 0, get(pw, texture + 32), 0, 0x20 + 0x10 * i, 0,
                         get(pw, texture + 16), get(pw, texture + 20), get(pw, texture + 24),
                         get(pw, texture + 28), 0});
    }
    out.insert(out.end(), records.begin(), records.end());
    out.resize(new_data);
    out.insert(out.end(), pw.begin() + data, pw.end());
    return out;
}

// The balloon and harness models with their packs, from the PW resources
// keyed `type << 24 | name hash`, as bundle file name -> bytes. PW scopes a
// model's texture names to its own pack; PO looks names up in one table, so a
// later pack gives up a name an earlier one uses (the harness reuses two of
// the balloon's for different images) and its model's one reference follows.
inline std::map<std::string, Bytes> models(const std::map<std::uint32_t, Bytes> &pw) {
    std::map<std::string, Bytes> out;
    std::vector<std::uint32_t> taken;
    const auto has = [](const std::vector<std::uint32_t> &list, std::uint32_t name) {
        return std::find(list.begin(), list.end(), name) != list.end();
    };
    for (const auto &[name, hash] : {std::pair{"fult_item_balloon", 0xf66bb7u}, std::pair{"fult_item_body", 0x430af6u}}) {
        if (!pw.contains(0x14000000u | hash) || !pw.contains(0x13000000u | hash))
            throw std::runtime_error(std::string("PW disc lacks ") + name);
        Bytes pack = txp(pw.at(0x14000000u | hash)), model = pw.at(0x13000000u | hash);
        if (get(model, 0) != 0x2050444du) throw std::runtime_error(std::string(name) + " is not a PSP model");
        const std::uint32_t textures = get(pack, 16), table = get(pack, 20);
        std::vector<std::uint32_t> names;
        for (std::uint32_t i = 0; i < textures; ++i) names.push_back(get(pack, table + 0x30u * i + 4));
        for (std::uint32_t i = 0; i < textures; ++i) {
            std::uint32_t renamed = names[i];
            while (has(taken, renamed) || (renamed != names[i] && has(names, renamed)))
                renamed = (renamed + 0x10000u) & 0xffffffu;
            taken.push_back(renamed);
            if (renamed == names[i]) continue;
            std::size_t reference = 0, references = 0;
            for (std::size_t at = 0; at + 4 <= model.size(); at += 4)
                if (get(model, at) == names[i]) { reference = at; ++references; }
            if (references != 1) throw std::runtime_error(std::string(name) + " does not name a texture exactly once");
            put(pack, table + 0x30u * i + 4, renamed);
            put(model, reference, renamed);
        }
        out[std::string(name) + ".txp"] = std::move(pack);
        out[std::string(name) + ".mdp"] = std::move(model);
    }
    return out;
}
// PO's 19-joint humanoid: joint name hashes and each joint's parent (255 for
// the root), as every PO soldier and player motion archive lists them. PW
// names its joints differently but orders and parents them the same.
constexpr std::uint32_t joint_names[19] = {
    0x72662d, 0x39e5be, 0x35d6ac, 0xc7226c, 0xc42227, 0xcb693c, 0xcc4e45, 0xc4ce85, 0xc5ce7c, 0x2b713d,
    0x2c5646, 0x24d686, 0x25d67d, 0x7f1c41, 0xc63f64, 0xc5162c, 0x801c4d, 0xc93fa4, 0x251e2d};
constexpr std::uint8_t joint_parents[19] = {255, 0, 1, 2, 3, 2, 5, 6, 7, 2, 9, 10, 11, 0, 13, 14, 0, 16, 17};

// Clips of a PW PSP motion archive as one PO Mtar. A PW archive may declare
// more joints than a clip animates (enedam declares 38, its clips use the
// first 19); each clip must animate exactly PO's joints in PO's order and
// hierarchy. PO indexes joints implicitly, so the clip's two PW byte tables
// are dropped and its header, offset table and compressed tracks are kept
// byte for byte. PW's separate effector data is not carried over.
inline Bytes mtar(std::span<const std::uint8_t> pw, std::initializer_list<std::uint32_t> clips) {
    constexpr std::uint32_t joints = 19, head = 64 + joints * 4;
    const std::uint32_t declared = get(pw, 8, 2), motions = get(pw, 10, 2), body = get(pw, 16), table = get(pw, 28);
    if (get(pw, 0) != 0x7261744du || get(pw, 12) != 0x10000u || declared < joints)
        throw std::runtime_error("Unsupported PW motion archive");
    std::vector<Bytes> blocks;
    for (auto clip : clips) {
        if (clip >= motions) throw std::runtime_error("PW motion archive lacks a clip");
        const std::uint64_t at = std::uint64_t(body) + get(pw, table + clip * 16ull), size = get(pw, table + clip * 16ull + 4);
        if (size < head + joints * 2 || at + size > pw.size()) throw std::runtime_error("PW clip is outside its archive");
        const auto block = pw.subspan(at, size);
        const std::uint32_t kind = get(block, 4), tracks = get(block, 16), units = get(block, 20);
        if ((kind != 0x36u && kind != 0x436u) || get(block, 8) != 5 || !get(block, 12) || get(block, 24) != joints ||
            get(block, 0x2e, 2) != head || tracks != head + joints * 2 || std::uint64_t(tracks) + units * 2ull > size)
            throw std::runtime_error("PW clip has an unsupported encoding or layout");
        for (std::uint32_t joint = 0; joint < joints; ++joint)
            if (block[head + joint] != joint || block[head + joints + joint] != joint_parents[joint] ||
                get(block, 64 + joint * 4) > units)
                throw std::runtime_error("PW clip does not animate PO's skeleton");
        for (std::uint32_t tail = 0; tail < 4; ++tail)
            if (get(block, 0x30 + tail * 4) > units) throw std::runtime_error("PW clip track is outside its data");
        Bytes converted(block.begin(), block.begin() + head);
        put(converted, 0x10, head);
        converted[0x2e] = converted[0x2f] = 0;
        converted.insert(converted.end(), block.begin() + tracks, block.begin() + tracks + units * 2);
        converted.resize((converted.size() + 15) & ~std::size_t(15));
        blocks.push_back(std::move(converted));
    }
    const std::uint32_t body_at = (32 + joints * 8 + std::uint32_t(blocks.size()) * 8 + 15) & ~15u;
    Bytes out;
    append(out, {0x7261744du, joints, std::uint32_t(blocks.size()), body_at, 0x10000u, 0, 0, 0});
    for (auto name : joint_names) append(out, {name});
    for (auto parent : joint_parents) append(out, {parent == 255 ? 0u : joint_names[parent]});
    std::uint32_t at = 0;
    for (const auto &block : blocks) { append(out, {at, std::uint32_t(block.size())}); at += std::uint32_t(block.size()); }
    out.resize(body_at);
    for (const auto &block : blocks) out.insert(out.end(), block.begin(), block.end());
    return out;
}
} // namespace mgs::fulton_convert
