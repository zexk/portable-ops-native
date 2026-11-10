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
    std::cout << "PW slot extraction and model conversion passed\n";
}
