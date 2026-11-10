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
} // namespace mgs::fulton_convert
