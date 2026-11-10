// Texture pack conversion on synthetic data; then, given a local Peace Walker
// PSP image (ULES-01372 1.01), test-pw-slot PW.iso [OUTPUT] converts the whole
// Fulton bundle, checks it and optionally writes it to OUTPUT. With a local
// Master Collection installation as well, test-pw-slot PW.iso OUTPUT MC
// MC_OUTPUT converts that too and checks it against the disc's.
#include "../profiles/po/host/fulton_convert.hpp"
#include "../profiles/po/host/pw_slot.hpp"
#include "psprecomp/sha256.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <tuple>

int main(int argc, char **argv) {
    {
        // One 64x64 4bpp image, two textures with their own CLUTs.
        using namespace mgs::fulton_convert;
        Bytes pw;
        append(pw, {0, 0xabcdef, 1, 2, 0x40, 0x20, 0x34, 0x90});
        append(pw, {0x60400004, 0x6040, 0, 0x110, 0});
        for (std::uint32_t i = 0; i < 2; ++i)
            append(pw, {0, 0x100 + i, 0x20, 0x90 + 0x40 * i, 0, 0, 0x3f800000, 0x3f800000, 0x400040, 0});
        pw.resize(0x90);
        for (unsigned i = 0; i < 0x80; ++i) pw.push_back(std::uint8_t(i));
        pw.resize(pw.size() + 0x800);
        const auto po = txp(pw);
        assert(get(po, 4) == 1 && get(po, 8) == 2 && get(po, 16) == 2);
        for (std::uint32_t i = 0; i < 2; ++i) {
            const auto image = get(po, 12) + 0x10 * i, texture = get(po, 20) + 0x30 * i;
            assert(get(po, image) == 0x60406040 && get(po, image + 4) == 4);
            assert(get(po, image + 8) % 16 == 0 && get(po, image + 12) % 16 == 0);
            assert(po[get(po, image + 12)] == 0x40 * i && po[get(po, image + 12) + 0x3f] == 0x40 * i + 0x3f);
            assert(get(po, texture) == 0x10 && get(po, texture + 4) == 0x100 + i && get(po, texture + 12) == 0x400040);
            assert(get(po, texture + 20) == image);
        }
        pw.resize(0x60);
        try { txp(pw); assert(false); } catch (const std::runtime_error &) {}
        std::cout << "PW texture pack conversion passed\n";
    }
    {
        // One PW clip over PO's 19 joints in an archive declaring 38.
        using namespace mgs::fulton_convert;
        const auto archive = [](std::uint32_t kind, std::uint8_t root_parent) {
            Bytes clip;
            append(clip, {123, kind, 5, 30, 64 + 19 * 4 + 38, 2, 19, 0, 0, 0, 0, std::uint32_t(64 + 19 * 4) << 16, 0, 0, 0, 0});
            clip.resize(64 + 19 * 4);
            for (std::uint8_t joint = 0; joint < 19; ++joint) clip.push_back(joint);
            for (auto parent : joint_parents) clip.push_back(parent);
            clip[64 + 19 * 4 + 19] = root_parent;
            for (char c : {'p', 'o', 's', 'e'}) clip.push_back(std::uint8_t(c));
            Bytes pw;
            const std::uint32_t table = 32 + 38 * 4;
            append(pw, {0x7261744d, 0, 38u | 1u << 16, 0x10000, table + 16, 0, 32, table});
            pw.resize(table);
            append(pw, {0, std::uint32_t(clip.size()), 0, 0});
            pw.insert(pw.end(), clip.begin(), clip.end());
            return pw;
        };
        const auto po = mtar(archive(0x436, 255), {0});
        const std::uint32_t body = get(po, 12);
        assert(get(po, 4) == 19 && get(po, 8) == 1 && body == 192 && get(po, 16) == 0x10000);
        assert(get(po, 32) == joint_names[0] && get(po, 32 + 76) == 0 && get(po, 32 + 80) == joint_names[0]);
        assert(get(po, body + 16) == 140 && get(po, body + 0x2e, 2) == 0 && po[body + 140] == 'p');
        assert(po.size() == body + 144 && po.size() % 16 == 0);
        for (const auto &bad : {archive(0x3e, 255), archive(0x36, 3)})
            try { mtar(bad, {0}); assert(false); } catch (const std::runtime_error &) {}
        try { mtar(archive(0x36, 255), {1}); assert(false); } catch (const std::runtime_error &) {}
        std::cout << "PW motion archive conversion passed\n";
    }
    if (argc < 2) return 0;
    const std::pair<std::uint32_t, const char *> expected[] = {
        {0x14f66bb7u, "1f83806787febd7d89392a0dba52bf27d283dcd29aaa77b6c3dfa542ec7af5ad"}, // fult_item_balloon.txp
        {0x14430af6u, "2ef1a41d2a7b1b4ffcc4edd4d6dfb8870d881d65bc34d6aa3607b50a21c3bde2"}, // fult_item_body.txp
        {0x13f66bb7u, "05bf11dc72a11641218897876a2c0b0dee93d29f2460c28d76e8fe9964c1cfd1"}, // fult_item_balloon.mdp
        {0x13430af6u, "dc0f106a06568d30c0488812312e735aa9f1796be5c6dcd3168cc3e05c14cb88"}, // fult_item_body.mdp
        {0x13000001u, nullptr},                                                             // not on the disc
    };
    std::vector<std::uint32_t> keys;
    for (const auto &[key, hash] : expected) keys.push_back(key);
    portablekit::IsoImage image(argv[1]);
    const auto iso = mgs::pw_slot::psp(image);
    const auto found = mgs::pw_slot::extract(iso, keys);
    assert(found.size() == 4 && !found.contains(0x13000001u));
    for (const auto &[key, hash] : expected) {
        if (!found.contains(key)) continue;
        const auto digest = psprecomp::sha256_bytes(found.at(key));
        std::cout << std::hex << key << std::dec << ' ' << found.at(key).size() << ' ' << digest << '\n';
        assert(!hash || digest == hash);
    }
    assert(found.at(0x13f66bb7u).size() == 8192 && found.at(0x13430af6u).size() == 8752);
    assert(std::memcmp(found.at(0x13f66bb7u).data(), "MDP ", 4) == 0);
    // The bundle: byte for byte what the Python tools wrote from the same disc.
    const auto bundle = mgs::fulton_convert::bundle(iso);
    for (const auto &[name, hash] : {
             std::pair{"fult_item_balloon.txp", "426b9dc80a36cb586fa602c6a8eabe5ac9667048cdb137738a61d4ef21973b03"},
             std::pair{"fult_item_body.txp", "90931257b47d78d6f78f23c4b776f3ce19bf6583fa8953d147fd17ba2f224b86"},
             std::pair{"fult_item_balloon.mdp", "05bf11dc72a11641218897876a2c0b0dee93d29f2460c28d76e8fe9964c1cfd1"},
             std::pair{"fult_item_body.mdp", "012249e527a5405e1751235304fb3d52773162eeaea3a5550543632ae1a96795"},
             std::pair{"fult_enemy.mtar", "5fc52d5c4234d551231b34310875270d7e497b49848bdb8442a7c7dab7b9ff03"},
             std::pair{"fult_place.mtar", "a82f6e3d55b77e8d982764cf6005f1e7167f1095ad751ef53359b6eb07ee3e56"},
             std::pair{"fulton.sep", "f15c1f262d09969bbe486306f13c0d73e942d9940f875daaa11181ba939974a1"}}) {
        const auto digest = psprecomp::sha256_bytes(bundle.at(name));
        std::cout << name << ' ' << digest << '\n';
        assert(digest == hash);
    }
    // The sequenced banks have no earlier reference; check their own tables.
    for (const auto &[name, id, count] : {std::tuple{"fulton-lift.sep", 0x32u, 2u}, std::tuple{"fulton-foley.sep", 0x144u, 3u}}) {
        using mgs::fulton_convert::get;
        const auto &bank = bundle.at(name);
        const std::uint32_t map = get(bank, 12), data = get(bank, 16), voices = get(bank, 20), end = get(bank, 28);
        assert(get(bank, 0) == (0x5053u | id << 16) && get(bank, 4) == bank.size() && bank.size() % 0x800 == 0);
        assert(get(bank, bank.size() - 4) == (0x5045u | id << 16) && (map - 0x30) / 32 == count && data - map == 1024);
        unsigned samples = 0;
        for (std::uint32_t key = 0; key < 128; ++key) {
            const std::uint32_t sample = get(bank, map + key * 8);
            if (sample == 0xffffffffu) continue;
            ++samples;
            assert(data + sample + 8 + get(bank, data + sample) <= voices && get(bank, map + key * 8 + 4) + 24 <= end - voices);
        }
        for (std::uint32_t cue = 0; cue < count; ++cue)
            assert(get(bank, end + get(bank, 0x30 + cue * 32 + 12)) == 0x44514553u);
        std::cout << name << ' ' << bank.size() << " bytes, " << samples << " samples\n";
        assert(samples > 0);
    }
    if (argc > 2) {
        std::filesystem::create_directories(argv[2]);
        for (const auto &[name, bytes] : bundle)
            std::ofstream(std::filesystem::path(argv[2]) / name, std::ios::binary)
                .write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
        std::cout << "bundle written to " << argv[2] << '\n';
    }
    if (argc > 4) {
        using mgs::fulton_convert::get;
        const auto wide = mgs::fulton_convert::bundle(mgs::pw_slot::master_collection(argv[3]));
        assert(wide.size() == 6);
        for (const char *name : {"fult_item_balloon", "fult_item_body"}) {
            // Models: the disc's, less the run of zeros its exporter left
            // between vertices and materials. Put the run back and the
            // files must be the same byte for byte.
            const auto &disc = bundle.at(std::string(name) + ".mdp");
            auto model = wide.at(std::string(name) + ".mdp");
            const std::uint32_t parts = get(model, 12), table = get(model, 28), materials = get(model, table + 20),
                                gap = std::uint32_t(disc.size() - model.size());
            assert(disc.size() > model.size() && gap % 4 == 0);
            model.insert(model.begin() + materials, gap, 0);
            for (std::uint32_t part = 0; part < parts; ++part) {
                const std::uint32_t at = table + part * 80;
                mgs::fulton_convert::put(model, at + 20, get(model, at + 20) + gap);
                for (std::uint32_t mesh = 0; mesh < get(model, at + 8); ++mesh) {
                    const std::uint32_t entry = get(model, at + 12) + mesh * 12 + 8;
                    mgs::fulton_convert::put(model, entry, get(model, entry) + gap);
                }
            }
            assert(model == disc);
            // Packs: the same tables, and the same pixels and palettes as far
            // as the disc's reach; both end in filler that differs.
            const auto &pack = wide.at(std::string(name) + ".txp"), &disc_pack = bundle.at(std::string(name) + ".txp");
            // The disc's last bytes are exporter filler, different in each edition.
            const auto [left, right] = std::mismatch(disc_pack.begin(), disc_pack.end(), pack.begin());
            const std::size_t used = std::size_t(left - disc_pack.begin());
            assert(pack.size() >= disc_pack.size() && used + 128 >= disc_pack.size());
            (void)right;
            std::cout << name << ": model equal with " << gap << " filler bytes restored, pack equal over " << used << " bytes\n";
        }
        for (const char *name : {"fult_enemy.mtar", "fult_place.mtar"}) {
            // Motions: the same clips at a finer precision. Same joint table,
            // clip names, kinds and frame counts; more track data.
            const auto &motion = wide.at(name), &disc = bundle.at(name);
            const std::uint32_t clips = get(disc, 8), body = get(motion, 12), disc_body = get(disc, 12);
            assert(get(motion, 8) == clips && body == disc_body && std::equal(disc.begin() + 32, disc.begin() + 184, motion.begin() + 32));
            for (std::uint32_t clip = 0; clip < clips; ++clip) {
                const std::uint32_t at = body + get(motion, 184 + clip * 8), disc_at = disc_body + get(disc, 184 + clip * 8);
                for (std::uint32_t field : {0u, 4u, 8u, 12u, 16u, 24u}) assert(get(motion, at + field) == get(disc, disc_at + field));
                assert(get(motion, at + 20) > get(disc, disc_at + 20));
            }
            std::cout << name << ": " << clips << " clips match the disc's, " << motion.size() << " bytes against "
                      << disc.size() << '\n';
        }
        std::filesystem::create_directories(argv[4]);
        for (const auto &[name, bytes] : wide)
            std::ofstream(std::filesystem::path(argv[4]) / name, std::ios::binary)
                .write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
        std::cout << "Master Collection bundle written to " << argv[4] << '\n';
    }
    std::cout << "PW extraction, model, motion and balloon sound conversion passed\n";
}
