// Texture pack conversion on synthetic data; then, given a local Peace Walker
// PSP image (ULES-01372 1.01), test-pw-slot PW.iso extracts the Fulton models
// and packs and checks the SHA-256 of both the raw and the converted files.
#include "../profiles/po/host/fulton_convert.hpp"
#include "../profiles/po/host/pw_slot.hpp"
#include "psprecomp/sha256.hpp"

#include <cassert>
#include <iostream>

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
    portablekit::IsoImage iso(argv[1]);
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
    // Converted, byte for byte what scripts/prepare-fulton-assets.py writes.
    const auto bundle = mgs::fulton_convert::models(found);
    for (const auto &[name, hash] : {
             std::pair{"fult_item_balloon.txp", "426b9dc80a36cb586fa602c6a8eabe5ac9667048cdb137738a61d4ef21973b03"},
             std::pair{"fult_item_body.txp", "90931257b47d78d6f78f23c4b776f3ce19bf6583fa8953d147fd17ba2f224b86"},
             std::pair{"fult_item_balloon.mdp", "05bf11dc72a11641218897876a2c0b0dee93d29f2460c28d76e8fe9964c1cfd1"},
             std::pair{"fult_item_body.mdp", "012249e527a5405e1751235304fb3d52773162eeaea3a5550543632ae1a96795"}}) {
        const auto digest = psprecomp::sha256_bytes(bundle.at(name));
        std::cout << name << ' ' << digest << '\n';
        assert(digest == hash);
    }
    // Motions: the first stage archive of each name whose clips convert.
    mgs::fulton_convert::Bytes lift, place, sound;
    unsigned archives = 0;
    mgs::pw_slot::stage_files(iso, [&](std::string_view name, std::span<const std::uint8_t> bytes) {
        try {
            if (lift.empty() && name == "enedam.mtar") { ++archives; lift = mgs::fulton_convert::mtar(bytes, {101, 103}); }
            if (place.empty() && name == "snake_coop.mtar") { ++archives; place = mgs::fulton_convert::mtar(bytes, {156}); }
            if (sound.empty() && name == "segrp_75_furuton.sep") sound = mgs::fulton_convert::bank(bytes, 0x31);
        } catch (const std::runtime_error &) {}
        return !lift.empty() && !place.empty() && !sound.empty();
    });
    std::cout << "motion archives tried " << archives << '\n';
    for (const auto &[bytes, hash] : {
             std::pair{&lift, "5fc52d5c4234d551231b34310875270d7e497b49848bdb8442a7c7dab7b9ff03"},   // fult_enemy.mtar
             std::pair{&place, "a82f6e3d55b77e8d982764cf6005f1e7167f1095ad751ef53359b6eb07ee3e56"},  // fult_place.mtar
             std::pair{&sound, "f15c1f262d09969bbe486306f13c0d73e942d9940f875daaa11181ba939974a1"}}) { // fulton.sep
        const auto digest = psprecomp::sha256_bytes(*bytes);
        std::cout << bytes->size() << ' ' << digest << '\n';
        assert(digest == hash);
    }
    std::cout << "PW extraction, model, motion and balloon sound conversion passed\n";
}
