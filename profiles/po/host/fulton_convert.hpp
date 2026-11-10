#pragma once

// Peace Walker Fulton resources to the port's converted bundle files. The
// Master Collection holds the PSP formats with their offsets widened to 64
// bits and tables padded to match ("wide" below); pixels, vertices and motion
// headers are otherwise the PSP ones.
#include "pw_slot.hpp"
#include "fulton_audio_convert.hpp"
#include "audio/atrac_decoder.hpp"

#include <algorithm>
#include <array>
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
// Wide: header tables at +0x18, +0x20, +0x28; image (0x20) pixels at +0x10;
// texture (0x30) {flags, hash, u64 image, u64 clut, u, v, su, sv, wh, 0}.
inline Bytes txp(std::span<const std::uint8_t> pw, bool wide = false) {
    const std::uint32_t images = get(pw, 8), textures = get(pw, 12), image_table = get(pw, wide ? 0x18 : 20),
                        texture_table = get(pw, wide ? 0x20 : 24), data = get(pw, wide ? 0x28 : 28);
    if (!images || !textures || textures > 256 || image_table < 0x20 || image_table >= texture_table ||
        texture_table >= data || data > pw.size())
        throw std::runtime_error("Not a PW texture pack");
    const std::uint32_t new_data = (0x20u + textures * 0x40u + 15u) & ~15u;
    Bytes out, records;
    append(out, {0, 1, textures, 0x20, textures, 0x20 + textures * 0x10, 0, 0});
    for (std::uint32_t i = 0; i < textures; ++i) {
        const std::size_t texture = texture_table + (wide ? 0x30u : 0x28u) * i, uv = texture + (wide ? 24 : 16);
        const std::uint32_t image = get(pw, texture + 8), clut = get(pw, texture + (wide ? 16 : 12)),
                            pixels = get(pw, image + (wide ? 16 : 12));
        if (pixels < data || pixels >= pw.size() || clut < data || clut >= pw.size())
            throw std::runtime_error("PW texture offsets are outside its data");
        append(out, {get(pw, image + 4, 2) << 16 | get(pw, image + 2, 2), get(pw, image, 2),
                     pixels - data + new_data, clut - data + new_data});
        append(records, {0x10, get(pw, texture + 4), 0, get(pw, uv + 16), 0, 0x20 + 0x10 * i, 0,
                         get(pw, uv), get(pw, uv + 4), get(pw, uv + 8), get(pw, uv + 12), 0});
    }
    out.insert(out.end(), records.begin(), records.end());
    out.resize(new_data);
    out.insert(out.end(), pw.begin() + data, pw.end());
    return out;
}

// A Master Collection model (MDPX) in the PSP layout PO loads (MDP). Both are
// header, bone records, part records, one mesh table per part, the vertex
// data and a table of materials. MDPX widens the header's two offsets, each
// part's three and each mesh's two, so its records are longer and everything
// behind them sits further in; vertex and material bytes are the same. The
// PSP exporter leaves a run of zeros between vertices and materials that
// nothing points at; it is not reproduced.
inline Bytes mdp(std::span<const std::uint8_t> wide) {
    const std::uint32_t bones = get(wide, 8), parts = get(wide, 12), bone_table = get(wide, 0x20),
                        part_table = get(wide, 0x28);
    if (get(wide, 0) != 0x5850444du || !parts || parts > 256 || bones > 256) throw std::runtime_error("Not a PW model");
    for (std::size_t at : {0x18u, 0x1cu, 0x24u, 0x2cu, 0x34u, 0x38u, 0x3cu, 0x40u, 0x44u, 0x48u, 0x4cu})
        if (get(wide, at)) throw std::runtime_error("PW model header has fields this converter does not know");
    if (wide.size() < 0x70 || std::uint64_t(bone_table) + bones * 16ull > wide.size() ||
        std::uint64_t(part_table) + parts * 0x70ull > wide.size())
        throw std::runtime_error("PW model tables are truncated");
    Bytes out{'M', 'D', 'P', ' '};
    out.insert(out.end(), wide.begin() + 4, wide.begin() + 0x18);
    const std::uint32_t new_parts = 0x50 + bones * 16;
    append(out, {0x50, new_parts, get(wide, 0x30), 0, 0, 0});
    out.insert(out.end(), wide.begin() + 0x50, wide.begin() + 0x70);
    out.insert(out.end(), wide.begin() + bone_table, wide.begin() + bone_table + bones * 16);

    // Where everything lands: tables after the parts, each on a 16-byte
    // boundary, then the vertex data, then the materials.
    std::uint32_t cursor = new_parts + parts * 80, vertices = 0xffffffffu, materials = 0, materials_end = 0;
    std::vector<std::uint32_t> tables(parts);
    for (std::uint32_t part = 0; part < parts; ++part) {
        const std::size_t at = part_table + part * 0x70ull;
        if (get(wide, at + 0x14) || get(wide, at + 0x1c) || get(wide, at + 0x24) || get(wide, at + 0x2c) ||
            get(wide, at + 0x30) || get(wide, at + 0x34) || get(wide, at + 0x38) || get(wide, at + 0x3c))
            throw std::runtime_error("PW model part has fields this converter does not know");
        if (std::uint64_t(get(wide, at + 0x10)) + get(wide, at + 8) * 24ull > wide.size())
            throw std::runtime_error("PW model mesh table is truncated");
        cursor = (cursor + 15) & ~15u;
        tables[part] = cursor;
        cursor += get(wide, at + 8) * 12;
        vertices = std::min(vertices, get(wide, at + 0x18));
        if (part && get(wide, at + 0x20) != materials) throw std::runtime_error("PW model parts disagree on materials");
        materials = get(wide, at + 0x20);
        for (std::uint32_t mesh = 0; mesh < get(wide, at + 8); ++mesh)
            materials_end = std::max(materials_end, get(wide, get(wide, at + 0x10) + mesh * 24ull + 16) + 24);
    }
    if (vertices > materials || materials > materials_end || materials_end > wide.size() || materials_end - materials > 0x10000)
        throw std::runtime_error("PW model regions are out of order");
    const std::uint32_t new_vertices = (cursor + 15) & ~15u,
                        new_materials = (new_vertices + (materials - vertices) + 15) & ~15u;
    for (std::uint32_t part = 0; part < parts; ++part) {
        const std::size_t at = part_table + part * 0x70ull;
        out.insert(out.end(), wide.begin() + at, wide.begin() + at + 12);
        append(out, {tables[part], get(wide, at + 0x18) - vertices + new_vertices, new_materials, get(wide, at + 0x28), 0});
        out.insert(out.end(), wide.begin() + at + 0x40, wide.begin() + at + 0x70);
    }
    for (std::uint32_t part = 0; part < parts; ++part) {
        const std::size_t at = part_table + part * 0x70ull, table = get(wide, at + 0x10);
        out.resize(tables[part]);
        for (std::uint32_t mesh = 0; mesh < get(wide, at + 8); ++mesh) {
            const std::size_t entry = table + mesh * 24ull;
            const std::uint32_t data = get(wide, entry + 8), material = get(wide, entry + 16);
            if (get(wide, entry + 4) || get(wide, entry + 12) || get(wide, entry + 20) || data < vertices ||
                data >= materials || material < materials)
                throw std::runtime_error("PW model mesh has fields this converter does not know");
            append(out, {get(wide, entry), data - vertices + new_vertices, material - materials + new_materials});
        }
    }
    out.resize(new_vertices);
    out.insert(out.end(), wide.begin() + vertices, wide.begin() + materials);
    out.resize(new_materials);
    out.insert(out.end(), wide.begin() + materials, wide.begin() + materials_end);
    return out;
}

// The balloon and harness models with their packs, from the PW resources
// keyed `type << 24 | name hash`, as bundle file name -> bytes. PW scopes a
// model's texture names to its own pack; PO looks names up in one table, so a
// later pack gives up a name an earlier one uses (the harness reuses two of
// the balloon's for different images) and its model's one reference follows.
inline std::map<std::string, Bytes> models(const std::map<std::uint32_t, Bytes> &pw, bool wide = false) {
    std::map<std::string, Bytes> out;
    std::vector<std::uint32_t> taken;
    const auto has = [](const std::vector<std::uint32_t> &list, std::uint32_t name) {
        return std::find(list.begin(), list.end(), name) != list.end();
    };
    for (const auto &[name, hash] : {std::pair{"fult_item_balloon", 0xf66bb7u}, std::pair{"fult_item_body", 0x430af6u}}) {
        if (!pw.contains(0x14000000u | hash) || !pw.contains(0x13000000u | hash))
            throw std::runtime_error(std::string("PW data lacks ") + name);
        Bytes pack = txp(pw.at(0x14000000u | hash), wide);
        Bytes model = wide ? mdp(pw.at(0x13000000u | hash)) : pw.at(0x13000000u | hash);
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
// A sound bank (SP + id, ..., EP + id at its declared size) under another id.
// PW's segrp_75_furuton, one ATRAC3plus clip, plays in PO as it is once it
// carries bank id 0x31, PO's own one-sound segrp_75 (sound 0x271).
inline Bytes bank(std::span<const std::uint8_t> pw, std::uint16_t id) {
    const std::uint32_t size = get(pw, 4);
    if (pw[0] != 'S' || pw[1] != 'P' || size < 0x34 || size > pw.size() || pw[size - 4] != 'E' || pw[size - 3] != 'P')
        throw std::runtime_error("Not a PW sound bank");
    Bytes out(pw.begin(), pw.end());
    std::memcpy(out.data() + 2, &id, 2);
    std::memcpy(out.data() + size - 2, &id, 2);
    return out;
}

// PO plays one ATRAC3plus effect at a time (sound2 type 5), and its soldiers
// speak in ATRAC3plus: a line of speech took the balloon's voice and cut it
// off. So the PSP balloon is decoded and stored as the 12 kHz ADPCM cue the
// MC import makes. Without FFmpeg (standalone checks) it stays ATRAC3plus.
inline Bytes balloon(std::span<const std::uint8_t> pw) {
    auto copy = bank(pw, 0x31);
    portablekit::audio::AtracDecoder decoder;
    if (!portablekit::audio::AtracDecoder::available()) return copy;
    const std::size_t riff = 0x54, end = get(pw, 4) - 4;
    if (get(pw, riff) != 0x46464952u) throw std::runtime_error("PW balloon sound is not ATRAC3plus");
    std::uint32_t channels = 0, rate = 0, block = 0, samples = 0, skip = 368, data = 0, size = 0;
    for (std::size_t at = riff + 12; at + 8 <= end;) {
        const auto id = get(pw, at), length = get(pw, at + 4);
        if (length > end - at - 8) throw std::runtime_error("PW balloon sound is truncated");
        if (id == 0x20746d66u && length >= 14) {
            channels = get(pw, at + 10, 2); rate = get(pw, at + 12); block = get(pw, at + 20, 2);
        } else if (id == 0x74636166u && length >= 8) {
            samples = get(pw, at + 8); skip += get(pw, at + 12);
        } else if (id == 0x61746164u) { data = std::uint32_t(at + 8); size = length; }
        at += 8 + length + (length & 1);
    }
    if (channels != 1 || !block || !data || !samples || samples > rate * 15u ||
        !decoder.open(portablekit::audio::AtracCodec::Atrac3Plus, 1, block, {}))
        throw std::runtime_error("Unsupported PW balloon sound");
    fulton_audio_convert::Clip clip{{}, rate};
    std::array<std::int16_t, 2048 * 2> frame;
    for (std::uint32_t at = 0; at + block <= size; at += block) {
        const auto count = decoder.decode(pw.subspan(data + at, block), frame.data());
        for (std::size_t i = 0; i < count; ++i) clip.samples.push_back(frame[i * 2]);
    }
    if (clip.samples.size() < skip + samples) throw std::runtime_error("Cannot decode PW balloon sound");
    clip.samples.erase(clip.samples.begin(), clip.samples.begin() + skip);
    clip.samples.resize(samples);
    return fulton_audio_convert::bank({clip}, 0x31, 12000, 0x8000);
}

// Chosen cues of a PW sequenced effects bank as a small bank of their own.
// Such a bank (PW's resident segrp_0_jochu_2other, PO's own
// segrp_70_stage_glass) has cue records from +0x08, a 128-entry key map of
// {sample, voice} offsets at +0x0C, samples ({length, format} + ADPCM) at
// +0x10, 24-byte voices at +0x14 and, after them at +0x1C, one "SEQD" note
// sequence per cue, found through the cue's +0x0C. In a sequence, 0x8A n
// selects key map entry n. PO's sound driver plays the same form, so cues,
// sequences, voices and samples are copied as they are; only the samples
// those cues select are kept and the offsets to them renumbered.
// An optional PSP grunt bank adds its direct ADPCM cue through a free key.
inline Bytes cues(std::span<const std::uint8_t> pw, std::uint16_t id, std::initializer_list<std::uint32_t> wanted,
                  std::uint32_t limit, std::span<const std::uint8_t> grunt = {}) {
    const std::uint32_t size = get(pw, 4), first = get(pw, 8), map = get(pw, 12), data = get(pw, 16),
                        voices = get(pw, 20), end = get(pw, 28);
    if (pw[0] != 'S' || pw[1] != 'P' || first != 0x30 || first >= map || data - map != 128 * 8 || data > voices ||
        voices > end || end > size || size > pw.size() || (end - voices) % 24)
        throw std::runtime_error("Not a PW sequenced sound bank");
    Bytes records, keys(128 * 8, 0xff), samples, sequences;
    Bytes instruments(pw.begin() + voices, pw.begin() + end);
    for (auto cue : wanted) {
        if (cue >= (map - first) / 32) throw std::runtime_error("PW sound bank lacks a cue");
        const auto record = pw.subspan(first + cue * 32u, 32);
        const std::uint64_t at = std::uint64_t(end) + get(record, 12), length = 8ull + get(pw, at + 4);
        if (get(record, 4) != 0 || get(pw, at) != 0x44514553u || at + length > size)
            throw std::runtime_error("PW sound cue is not a note sequence");
        for (std::uint64_t i = at + 8; i + 1 < at + length; ++i) {
            const std::uint32_t key = pw[i + 1];
            if (pw[i] != 0x8a || key >= 128 || get(keys, key * 8) != 0xffffffffu || get(pw, map + key * 8) == 0xffffffffu)
                continue;
            const std::uint64_t sample = std::uint64_t(data) + get(pw, map + key * 8), bytes = 8ull + get(pw, sample);
            if (sample + bytes > voices || get(pw, map + key * 8 + 4) + 24 > end - voices)
                throw std::runtime_error("PW sound sample is outside its bank");
            put(keys, key * 8, std::uint32_t(samples.size()));
            put(keys, key * 8 + 4, get(pw, map + key * 8 + 4));
            samples.insert(samples.end(), pw.begin() + sample, pw.begin() + sample + bytes);
        }
        records.insert(records.end(), record.begin(), record.end());
        put(records, records.size() - 32 + 12, std::uint32_t(sequences.size()));
        sequences.insert(sequences.end(), pw.begin() + at, pw.begin() + at + length);
    }
    if (!grunt.empty()) {
        // ULES01372 1.01, SLOT 0x3716B682, bank 0xD67 cue 6: the user
        // confirmed this against MC's yank grunt. Keep its ADPCM and voice
        // unchanged, alongside the three sequenced attachment cues.
        constexpr std::uint32_t record = 0x30 + 6 * 32;
        const std::uint64_t size = get(grunt, 4), map = get(grunt, 12), data = get(grunt, 16),
                            voices = get(grunt, 24), end = get(grunt, 28);
        if (get(grunt, 0) != 0x0d675053u || size > grunt.size() || size < 4 ||
            get(grunt, size - 4) != 0x0d675045u || get(grunt, 8) != 0x30 || map < record + 32 ||
            data < map || voices < data || end < voices || end > size || get(grunt, record + 4) != 0x01010100u)
            throw std::runtime_error("Unsupported PSP soldier voice bank");
        const std::uint64_t index = get(grunt, record + 8);
        if (index % 8 || index + 8 > data - map) throw std::runtime_error("Invalid PSP grunt index");
        const std::uint64_t sample = data + get(grunt, map + index), voice = voices + get(grunt, map + index + 4),
                            length = get(grunt, sample);
        if (sample + 8 + length > voices || !length || length % 16 || voice + 24 > end)
            throw std::runtime_error("Invalid PSP grunt sample or voice");
        unsigned key = 0;
        while (key < 128 && get(keys, key * 8) != 0xffffffffu) ++key;
        if (key == 128) throw std::runtime_error("No free sound key for PSP grunt");
        put(keys, key * 8, std::uint32_t(samples.size()));
        put(keys, key * 8 + 4, std::uint32_t(instruments.size()));
        samples.insert(samples.end(), grunt.begin() + sample, grunt.begin() + sample + 8 + length);
        instruments.insert(instruments.end(), grunt.begin() + voice, grunt.begin() + voice + 24);
        records.insert(records.end(), grunt.begin() + record, grunt.begin() + record + 32);
        put(records, records.size() - 32 + 8, key * 8);
    }
    Bytes out{'S', 'P', std::uint8_t(id), std::uint8_t(id >> 8)};
    const std::uint32_t new_map = 0x30 + std::uint32_t(records.size()), new_data = new_map + 128 * 8,
                        new_voices = new_data + std::uint32_t(samples.size()), new_end = new_voices + std::uint32_t(instruments.size()),
                        new_size = (new_end + std::uint32_t(sequences.size()) + 4 + 0x7ffu) & ~0x7ffu;
    if (new_size > limit) throw std::runtime_error("PW sound cues do not fit PO's bank");
    append(out, {new_size, 0x30, new_map, new_data, new_voices, new_voices, new_end});
    out.insert(out.end(), pw.begin() + 0x20, pw.begin() + 0x30);
    for (const Bytes *part : {&records, &keys, &samples}) out.insert(out.end(), part->begin(), part->end());
    out.insert(out.end(), instruments.begin(), instruments.end());
    out.insert(out.end(), sequences.begin(), sequences.end());
    out.resize(new_size - 4);
    out.insert(out.end(), {'E', 'P', std::uint8_t(id), std::uint8_t(id >> 8)});
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
// Wide: clip table records are {u64 offset, u64 size, ...}, 32 bytes.
inline Bytes mtar(std::span<const std::uint8_t> pw, std::initializer_list<std::uint32_t> clips, bool wide = false) {
    constexpr std::uint32_t joints = 19, head = 64 + joints * 4;
    const std::uint64_t record = wide ? 32 : 16;
    const std::uint32_t declared = get(pw, 8, 2), motions = get(pw, 10, 2), body = get(pw, 16), table = get(pw, 28);
    if (get(pw, 0) != 0x7261744du || get(pw, 12) != 0x10000u || declared < joints)
        throw std::runtime_error("Unsupported PW motion archive");
    std::vector<Bytes> blocks;
    for (auto clip : clips) {
        if (clip >= motions) throw std::runtime_error("PW motion archive lacks a clip");
        const std::uint64_t at = std::uint64_t(body) + get(pw, table + clip * record),
                            size = get(pw, table + clip * record + (wide ? 8 : 4));
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
// The whole converted bundle from a PW source, as file name -> bytes. Models,
// packs and the lift motion are required; a sound bank or the placement
// motion the source does not yield in a usable form is left out. A name can
// occur in several stage entities, not all of them usable (two of the PSP
// disc's five snake_coop archives are not): the first that converts wins.
// The MC motion tracks retain their finer precision; its Ogg effects become
// direct ADPCM cues. Both editions produce the same runtime representation.
inline std::map<std::string, Bytes> bundle(const pw_slot::Source &source) {
    const bool wide = source.wide;
    std::vector<std::uint32_t> keys = {0x14f66bb7u, 0x14430af6u, 0x13f66bb7u, 0x13430af6u};
    // MC bank 0xD88 lives in SLOT under this resource key, not in stages.
    // Its PSP counterpart is bank 0xD67; bank ids differ between editions.
    keys.push_back(0x3716b682u);
    const auto resources = pw_slot::extract(source, keys);
    std::vector<fulton_audio_convert::Clip> foley;
    if (wide && (!resources.contains(0x3716b682u) || get(resources.at(0x3716b682u), 0) != 0x0d885053u))
        throw std::runtime_error("Master Collection data lacks the supported soldier voice bank");
    auto out = models(resources, wide);
    const auto take = [&out](const char *file, auto make) {
        if (out.contains(file)) return;
        try { out[file] = make(); } catch (const std::runtime_error &) {}
    };
    pw_slot::stage_files(source, [&](std::string_view name, std::span<const std::uint8_t> bytes) {
        // enedam clips 101 and 103: the yank and the hang. snake_coop 156: the attach.
        if (name == "enedam.mtar") take("fult_enemy.mtar", [&] { return mtar(bytes, {101, 103}, wide); });
        if (name == "snake_coop.mtar") take("fult_place.mtar", [&] { return mtar(bytes, {156}, wide); });
        if (wide) {
            using fulton_audio_convert::cue;
            if (name == "segrp_75_furuton.sep")
                // ponytail: 12 kHz fits the 4.36 s effect in PO's 32 KiB slot;
                // use a larger private bank if full-bandwidth audio is needed.
                take("fulton.sep", [&] { return fulton_audio_convert::bank({cue(bytes, 0)}, 0x31, 12000, 0x8000); });
            if (name == "segrp_0_jochu_2other.sep") {
                take("fulton-lift.sep", [&] {
                    return fulton_audio_convert::bank({cue(bytes, 104), cue(bytes, 105)}, 0x32, 44100, 0x11000);
                });
                if (foley.empty()) foley = {cue(bytes, 32), cue(bytes, 74), cue(bytes, 37)};
            }
            return out.size() == 8 && foley.size() == 3;
        }
        if (name == "segrp_75_furuton.sep") take("fulton.sep", [&] { return balloon(bytes); });
        if (name == "segrp_0_jochu_2other.sep") {
            // Sounds 0x9D3B and 0x9D3C (yank, release) as PO bank 0x32; attach
            // effects 0x9CF3, 0x9D1D and 0x9CF8 as the port's private bank.
            take("fulton-lift.sep", [&] { return cues(bytes, 0x32, {104, 105}, 0x11000); });
            take("fulton-foley.sep", [&] {
                const auto voice = resources.find(0x3716b682u);
                return cues(bytes, 0x144, {32, 74, 37}, 0x9000,
                            voice == resources.end() ? std::span<const std::uint8_t>{} : voice->second);
            });
        }
        return out.size() == 9;
    });
    if (wide) {
        // MC build 25294658, PE 0x6AA628A0: confirmed ordinary-soldier yank
        // vocal, bank 0xD88 cue 6. Other voice families remain unmapped.
        if (foley.size() == 3) foley.push_back(fulton_audio_convert::cue(resources.at(0x3716b682u), 6));
        if (!foley.empty()) out["fulton-foley.sep"] = fulton_audio_convert::bank(foley, 0x144, 44100, 0x9000);
        if (out.size() != 9 || foley.size() != 4)
            throw std::runtime_error("Master Collection data lacks a supported Fulton motion or sound");
    }
    if (!out.contains("fult_enemy.mtar")) throw std::runtime_error("PW data lacks the soldier lift motion");
    return out;
}
} // namespace mgs::fulton_convert
