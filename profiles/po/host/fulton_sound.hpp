#pragma once

// The Fulton sound: PW's segrp_75_furuton bank, played through PO's own sound
// system (PO ULUS-10202 1.02; PW PSP EU ULES01372 1.01 donor).
//
// PO describes every sound bank in an SHDS table inside its executable: bank
// ids 41-323, each with a size, a slot.dat offset and its first sound id
// (sound id = first id + cue). Bank 0x31 is PO's own segrp_75 slot: one sound,
// id 0x271 (an ATRAC3plus record), up to 0x8000 bytes; no stage loads it.
// PW's Fulton bank has the same container and one ATRAC3plus cue;
// prepare-fulton-assets.py renumbers it 0x0D1F -> 0x31 and writes fulton.sep.
//
// Loading: the bank lives in the engine heap (0x3A478, pool 0), which a stage
// load resets: a bank filed earlier is overwritten by that stage's own banks.
// So it is filed per stage, when the first Fulton of the stage starts, through
// the .sep handler 0xC918 (data, key, mode 2, size, flags 1). Flags bit 0
// picks the slot tables; the loader's plain path (flags 0) refuses while the
// context is locked (+0x284 bit 0), as it is in play. On later stages our
// entry in the engine's list is pointed at the new copy and registered with
// sound2 again (0x1C5BC0, as the handler does), so the list does not grow.
// Not yet working: sound2 accepts the bank (0x1C5BC0 only validates it and
// returns its id) but its play routine 0x08B12644 refuses sound 0x271.
// Playing: 0xE540(id, 0, 0, &position) is the engine's sound call; a position
// makes it a 3D sound.

#include "fulton_assets.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

namespace mgs::fulton_sound {
constexpr std::uint32_t base = 0x08804000u;
constexpr std::uint32_t kHandler = base + 0xc918u, kRegister = base + 0x1c5bc0u;
constexpr std::uint32_t kAlloc = base + 0x3a478u, kPlay = base + 0xe540u;
constexpr std::uint32_t kKey = 0x37000000u | 0x89cdf4u; // .sep, GV_StrCode("segrp_75_furuton")
constexpr std::uint32_t kSound = 0x271u;
// The sound context. Code loads it as 0x7FAD8 relative to the data segment
// (ELF 0x1EED40): a relocatable image, so data offsets are per segment.
constexpr std::uint32_t kContext = base + 0x1eed40u + 0x7fad8u;

inline std::vector<std::uint8_t> bank;
inline bool current = false; // filed since the last stage change

// Our entry in the engine's bank lists (sound context: tables at +0x28C/+0x50C
// or counted lists at +0x04/+0x790; 0x14-byte entries, data at +0, name hash
// at +0x10, size at +0xC), or 0.
template<class Memory>
std::uint32_t entry(const Memory &memory) {
    for (std::uint32_t at = kContext + 4u; at < kContext + 0x790u + 64u * 0x14u; at += 4u)
        if (memory.load32(at + 0x10u) == (kKey & 0xffffffu) && memory.load32(at) &&
            memory.load32(at + 0xcu) == bank.size())
            return at;
    return 0;
}

inline void stage_changed() { current = false; }

// Files the bank for this stage if needed, then next.
inline void prepare(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx,
                    std::function<void(psprecomp::AllegrexContext &)> next) {
    if (current || bank.empty()) { next(ctx); return; }
    portablekit::hle_extension::call_guest(ctx, kAlloc, {0u, std::uint32_t(bank.size()), 0u},
        [&runtime, next](auto &context, std::uint32_t buffer) {
            auto &memory = runtime.memory();
            if (!buffer || !memory.contains(buffer, bank.size())) {
                std::cerr << "[fulton] no memory for the sound bank" << std::endl;
                next(context);
                return;
            }
            memory.copy_in(buffer, bank);
            auto done = [next](auto &filed, std::uint32_t result) {
                current = true;
                std::cout << "[fulton] sound bank filed for this stage (" << result << ")" << std::endl;
                next(filed);
            };
            if (const auto at = entry(memory)) {
                memory.store32(at, buffer);
                portablekit::hle_extension::call_guest(context, kRegister, {buffer, buffer, kKey}, done);
            } else {
                portablekit::hle_extension::call_guest(context, kHandler,
                    {buffer, kKey, 2u, std::uint32_t(bank.size()), 1u}, done);
            }
        });
}

// Plays the Fulton sound at a guest position vector (3 floats), or 2D with 0.
template<class Next>
void play(psprecomp::AllegrexContext &ctx, std::uint32_t position, Next next) {
    if (!current) { next(ctx); return; }
    if (portablekit::hle_extension::env("FULTON_SOUND_2D")) position = 0;
    portablekit::hle_extension::call_guest(ctx, kPlay, {kSound, 0u, 0u, position},
        [next](auto &context, std::uint32_t handle) {
            if (!handle) std::cerr << "[fulton] sound 0x271 refused" << std::endl;
            next(context);
        });
}

inline void install(psprecomp::Runtime &) {
    if (!fulton_assets::enabled()) return;
    const char *directory = portablekit::hle_extension::env("FULTON_STAGE_DIR");
    if (!directory) return;
    std::ifstream file(std::filesystem::path(directory) / "fulton.sep", std::ios::binary);
    bank.assign(std::istreambuf_iterator<char>(file), {});
    // Same checks sound2 makes: SP + id at the start, EP + id at the end.
    if (bank.size() < 0x40 || bank[0] != 'S' || bank[1] != 'P' || bank[2] != 0x31 || bank[3] != 0 ||
        bank[bank.size() - 4] != 'E' || bank[bank.size() - 3] != 'P' || bank[bank.size() - 2] != 0x31) {
        if (!bank.empty()) std::cerr << "[fulton] fulton.sep is not a bank 0x31" << std::endl;
        bank.clear();
    }
}
} // namespace mgs::fulton_sound
