// Needs a local Peace Walker PSP image (ULES-01372 1.01): test-pw-slot PW.iso
// Extracts the Fulton models and texture packs and checks their SHA-256.
#include "../profiles/po/host/pw_slot.hpp"
#include "psprecomp/sha256.hpp"

#include <cassert>
#include <iostream>

int main(int argc, char **argv) {
    if (argc < 2) { std::cerr << "usage: test-pw-slot PW.iso\n"; return 2; }
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
    std::cout << "PW slot extraction passed\n";
}
