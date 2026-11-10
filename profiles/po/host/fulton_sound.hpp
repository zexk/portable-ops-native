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
// Filing only validates the bank with sound2 (0x1C5BC0 returns its id); sound2
// plays a bank once its entry in the SHDS bank table holds the data pointer,
// set through 0x1C5B20 (bank id, data) as engine 0xD1A8 does on demand.
// Playing: 0xE540(id, 0, 0, &position) is the engine's sound call; a position
// makes it a 3D sound.

#include "fulton_assets.hpp"
#include "fulton_audio_catalog.hpp"
#include "kernel/kernel.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

namespace mgs::fulton_sound {
constexpr std::uint32_t base = 0x08804000u;
constexpr std::uint32_t kHandler = base + 0xc918u, kRegister = base + 0x1c5bc0u, kActivate = base + 0x1c5b20u;
constexpr std::uint32_t kAlloc = base + 0x3a478u, kPlay = base + 0xe540u;
constexpr std::uint32_t kKey = 0x37000000u | 0x89cdf4u; // .sep, GV_StrCode("segrp_75_furuton")
constexpr std::uint32_t kSound = 0x271u, kSound2 = 0x08b10100u;
constexpr std::uint32_t kFoleySound = fulton_audio_catalog::kFirstSound;
constexpr std::uint32_t kFoleyKey = 0x37d1baeau; // fult_foley
inline std::uint32_t catalog_address = 0;
inline std::int32_t catalog_block = -1;
// Two common extraction effects captured in PW MC as 0x9D3B/0x9D3C.
// Bank 0x32 has two PO slots (0x272/0x273), 0x11000 bytes, and no stage loads it.
constexpr std::uint32_t kLiftSound = 0x272u, kLiftKey = 0x37376f7du;
// The sound context. Code loads it as 0x7FAD8 relative to the data segment
// (ELF 0x1EED40): a relocatable image, so data offsets are per segment.
constexpr std::uint32_t kContext = base + 0x1eed40u + 0x7fad8u;

inline std::vector<std::uint8_t> bank;
inline std::vector<std::uint8_t> lift_bank, foley_bank;
inline bool foley_current = false;
inline bool current = false; // filed since the last stage change
inline bool lift_current = false;

// Our entry in the engine's bank lists (sound context: tables at +0x28C/+0x50C
// or counted lists at +0x04/+0x790; 0x14-byte entries, data at +0, name hash
// at +0x10, size at +0xC), or 0.
template<class Memory>
std::uint32_t entry(const Memory &memory, const std::vector<std::uint8_t> &bytes, std::uint32_t key) {
    for (std::uint32_t at = kContext + 4u; at < kContext + 0x790u + 64u * 0x14u; at += 4u)
        if (memory.load32(at + 0x10u) == (key & 0xffffffu) && memory.load32(at) &&
            memory.load32(at + 0xcu) == bytes.size())
            return at;
    return 0;
}

inline void stage_changed() { current = lift_current = foley_current = false; }

// Files the bank for this stage if needed, then next.
inline void prepare_bank(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx,
                         const std::vector<std::uint8_t> &bytes, bool &ready,
                         std::uint32_t key, std::uint32_t id,
                         std::function<void(psprecomp::AllegrexContext &)> next) {
    if (bytes.empty()) { next(ctx); return; }
    if (ready) {
        // Mission retry can clear sound lists without reloading mgp_stage.
        // A host-side "current" flag alone then leaves every later lift mute.
        auto &memory = runtime.memory();
        const auto at = entry(memory, bytes, key);
        const auto buffer = at ? memory.load32(at) : 0u;
        if (memory.contains(buffer, bytes.size()) &&
            memory.load32(buffer) == (0x5053u | id << 16u) &&
            memory.load32(buffer + 4u) == bytes.size() &&
            memory.load32(buffer + std::uint32_t(bytes.size()) - 4u) == (0x5045u | id << 16u)) {
            portablekit::hle_extension::call_guest(ctx, kActivate, {id, buffer, 0u},
                [next](auto &active, std::uint32_t) { next(active); });
            return;
        }
        ready = false;
        std::cout << "[fulton] sound bank 0x" << std::hex << id << std::dec
                  << " lost during stage reset; reloading" << std::endl;
    }
    portablekit::hle_extension::call_guest(ctx, kAlloc, {0u, std::uint32_t(bytes.size()), 0u},
        [&runtime, &bytes, &ready, key, id, next](auto &context, std::uint32_t buffer) {
            auto &memory = runtime.memory();
            if (!buffer || !memory.contains(buffer, bytes.size())) {
                std::cerr << "[fulton] no memory for the sound bank" << std::endl;
                next(context);
                return;
            }
            memory.copy_in(buffer, bytes);
            // Filing only validates the bank with sound2. Playable takes its
            // data pointer in sound2's bank entry: what engine 0xD1A8 does for
            // a sound group the scenario asks for, through stub 0x1C5B20
            // (sound2 0xE43865EC: bank id, data).
            auto done = [&runtime, &bytes, &ready, key, id, buffer, next](auto &filed, std::uint32_t result) {
                if (const auto at = entry(runtime.memory(), bytes, key))
                    runtime.memory().store32(at + 8u, runtime.memory().load32(at + 8u) | 1u); // active, as 0xD1A8 marks it
                portablekit::hle_extension::call_guest(filed, kActivate, {id, buffer, 0u},
                    [&ready, id, result, next](auto &active, std::uint32_t) {
                        ready = true;
                        std::cout << "[fulton] sound bank 0x" << std::hex << id << std::dec
                                  << " filed and active for this stage (" << result << ")" << std::endl;
                        next(active);
                    });
            };
            if (const auto at = entry(memory, bytes, key)) {
                memory.store32(at, buffer);
                portablekit::hle_extension::call_guest(context, kRegister, {buffer, buffer, key}, done);
            } else {
                portablekit::hle_extension::call_guest(context, kHandler,
                    {buffer, key, 2u, std::uint32_t(bytes.size()), 1u}, done);
            }
        });
}

// sound2's bank table: the executable's SHDS header (ELF 0x1D779C) gets its
// bank table pointer at +0x18 from sound2 0x1008; 16-byte entries indexed by
// bank id: data (0 = not playable), data, capacity, state byte at +0xC.
constexpr std::uint32_t kShds = base + 0x1d779cu;

// Keep the private catalogue outside the engine's stage-reset heaps.
// Sound2 may be initialized again: then its original catalogue is authoritative.
template<class Memory>
std::uint32_t catalog(const Memory &memory) {
    return catalog_address && memory.contains(kSound2 + 0x2d340u, 4) &&
           memory.load32(kSound2 + 0x2d340u) == catalog_address ? catalog_address : kShds;
}

inline bool prepare_catalog(psprecomp::Runtime &runtime) {
    auto &memory = runtime.memory();
    if (catalog(memory) == catalog_address && catalog_address) return true;
    if (!memory.contains(kSound2 + 0x2d364u, 4) || memory.load32(kSound2 + 0x2d340u) != kShds)
        return false;
    auto &kernel = portablekit::kernel();
    auto block = kernel.find_block(catalog_block);
    if (!block || block->name != "FultonAudioCatalog") {
        catalog_block = kernel.allocate_block("FultonAudioCatalog", 1, 0x8000, 0);
        block = kernel.find_block(catalog_block);
    }
    if (!block) return false;
    auto bytes = fulton_audio_catalog::extend(memory, kShds, block->address);
    if (bytes.empty() || bytes.size() > block->size) return false;
    memory.copy_in(block->address, bytes);
    catalog_address = block->address;
    memory.store32(kSound2 + 0x2d35cu, fulton_audio_catalog::kFirstSound + fulton_audio_catalog::kSounds);
    memory.store32(kSound2 + 0x2d360u, fulton_audio_catalog::kBank + 1);
    memory.store32(kSound2 + 0x2d340u, catalog_address);
    return true;
}

inline std::uint32_t bank_word(std::span<const std::uint8_t> bytes, std::size_t at) {
    return at + 4 > bytes.size() ? 0u
        : std::uint32_t(bytes[at]) | std::uint32_t(bytes[at + 1]) << 8u | std::uint32_t(bytes[at + 2]) << 16u |
          std::uint32_t(bytes[at + 3]) << 24u;
}
inline std::uint32_t cue_count(std::span<const std::uint8_t> bytes) {
    return (bank_word(bytes, 12) - bank_word(bytes, 8)) / 32u;
}
// A bank whose cues are note sequences over a 128-entry key map (PW's common
// effects, PO's segrp_70_stage_glass) rather than direct ADPCM samples.
inline bool sequenced(std::span<const std::uint8_t> bytes) {
    return bank_word(bytes, 16) - bank_word(bytes, 12) == 128u * 8u && bank_word(bytes, 0x34) == 0u;
}

// The SHDS sound table (header +0x34, 8 bytes an id) gives each sound a type
// in byte 0's low nibble, which sound2 0x5440 puts into the play id: 0 a note
// sequence (0x40010100, as the glass sound 0x253), 3 an ADPCM effect
// (0x40010103), 5 ATRAC3plus. It types 0x272 as ATRAC3plus; type 5 effects
// share voices 0x1C-0x21 with one playing at a time (sound2 0x2D2D0 = 1), so
// an ADPCM 0x272 started beside the balloon 0x271 took the balloon's voice
// and then failed ATRAC setup: neither played. No stock bank uses 0x272. So
// each of our sounds gets the type its own bank's cues have.
template<class Memory>
void retype(Memory &memory, std::uint32_t sound, std::uint32_t count, std::span<const std::uint8_t> bytes) {
    const auto header = catalog(memory);
    const auto cues = memory.load32(header + 0x14u);
    const auto to = (cues ? cues : header + 0x34u) + sound * 8u;
    if (!memory.contains(to, count * 8u) || memory.load32(kShds) != 0x53444853u) return;
    for (std::uint32_t i = 0; i < count; ++i) {
        memory.store32(to + i * 8u, sequenced(bytes) ? 0x40010100u : 0x40010103u);
        memory.store32(to + i * 8u + 4u, 0u);
    }
}

inline void prepare(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx,
                    std::function<void(psprecomp::AllegrexContext &)> next) {
    if (!foley_bank.empty() && !prepare_catalog(runtime)) {
        foley_current = false;
        std::cerr << "[fulton] private audio catalogue unavailable; extra cues disabled" << std::endl;
    }
    if (!lift_bank.empty()) retype(runtime.memory(), kLiftSound, 2u, lift_bank);
    if (!foley_bank.empty() && catalog(runtime.memory()) == catalog_address && catalog_address)
        retype(runtime.memory(), kFoleySound, fulton_audio_catalog::kSounds, foley_bank);
    prepare_bank(runtime, ctx, bank, current, kKey, 0x31u, [&runtime, next](auto &context) {
        prepare_bank(runtime, context, lift_bank, lift_current, kLiftKey, 0x32u,
            [&runtime, next](auto &loaded) {
                if (foley_bank.empty() || catalog(runtime.memory()) != catalog_address || !catalog_address) {
                    next(loaded); return;
                }
                prepare_bank(runtime, loaded, foley_bank, foley_current, kFoleyKey,
                             fulton_audio_catalog::kBank, next);
            });
    });
}
// sound2 0x2544 (play) leaves why it refused in this word: 1 bank not loaded,
// 2 no voice, 4 already playing, 8 on-demand load failed. sound2 is loaded at
// 0x08B10100 in this build; trusted only while its SHDS pointer matches.


template<class Memory>
std::uint32_t shds_entry(const Memory &memory, std::uint32_t id) {
    if (!memory.contains(kShds, 0x20u) || memory.load32(kShds) != 0x53444853u) return 0;
    const auto at = memory.load32(catalog(memory) + 0x18u) + id * 16u;
    return memory.contains(at, 16u) ? at : 0u;
}

// Plays the Fulton sound at a guest position vector (3 floats), or 2D with 0.
// E540 accepts only [0x144, 0xB9B): appended ids must use its sound2 path.
// Mirror its listener update and positional call, leaving stock ids unchanged.
template<class Next>
void dispatch(psprecomp::AllegrexContext &ctx, std::uint32_t sound,
              std::uint32_t position, Next next) {
    if (sound < kFoleySound || sound >= kFoleySound + fulton_audio_catalog::kSounds) {
        portablekit::hle_extension::call_guest(ctx, kPlay, {sound, 0u, 0u, position}, next);
    } else if (!position) {
        portablekit::hle_extension::call_guest(ctx, base + 0x1c5ad8u, {sound, 0u, 0u, 0u}, next);
    } else {
        portablekit::hle_extension::call_guest(ctx, base + 0x14a08u, {},
            [sound, position, next](auto &listener, std::uint32_t matrix) {
                portablekit::hle_extension::call_guest(listener, base + 0x1c5b68u, {matrix},
                    [sound, position, next](auto &ready, std::uint32_t) {
                        portablekit::hle_extension::call_guest(ready, base + 0x1c5b90u,
                                                               {sound, position, 0u, 0u}, next);
                    });
            });
    }
}

// sound2's bank entry can be cleared between the stage-start activation and
// this call (live PO 1.02: after a stage reinit both banks read data 0 and
// every play was refused), so the bank is activated again first when needed.
template<class Memory, class Next>
void play(const Memory &memory, psprecomp::AllegrexContext &ctx, std::uint32_t position, Next next,
          std::uint32_t sound = kSound) {
    const bool foley = sound >= kFoleySound && sound < kFoleySound + fulton_audio_catalog::kSounds;
    if (!(sound == kSound ? current : foley ? foley_current : lift_current)) { next(ctx); return; }
    // A bank may hold fewer cues than its ids (no soldier grunt from a PSP disc).
    if (foley && sound - kFoleySound >= cue_count(foley_bank)) { next(ctx); return; }
    if (portablekit::hle_extension::env("FULTON_SOUND_2D")) position = 0;
    const std::uint32_t id = sound == kSound ? 0x31u : foley ? fulton_audio_catalog::kBank : 0x32u;
    const auto &bytes = sound == kSound ? bank : foley ? foley_bank : lift_bank;
    auto start = [&memory, position, next, sound, id](psprecomp::AllegrexContext &context) {
        dispatch(context, sound, position,
            [&memory, next, sound, id](auto &played, std::uint32_t handle) {
                if (!handle) {
                    std::cerr << std::hex << "[fulton] sound 0x" << sound << " refused";
                    if (memory.contains(kSound2 + 0x2d340u, 4u) && memory.load32(kSound2 + 0x2d340u) == catalog(memory))
                        std::cerr << " (sound2 reason " << memory.load32(kSound2 + 0x2d2d4u) << ')';
                    if (const auto at = shds_entry(memory, id))
                        std::cerr << " bank 0x" << id << " data " << memory.load32(at) << " state "
                                  << (memory.load32(at + 0xcu) & 0xffu);
                    std::cerr << std::dec << std::endl;
                }
                next(played);
            });
    };
    const auto listed = bytes.empty() ? 0u : entry(memory, bytes, sound == kSound ? kKey : foley ? kFoleyKey : kLiftKey);
    const auto buffer = listed ? memory.load32(listed) : 0u;
    const auto at = shds_entry(memory, id);
    if (!buffer || !at || memory.load32(at) == buffer) { start(ctx); return; }
    std::cout << std::hex << "[fulton] sound bank 0x" << id << " released (data " << memory.load32(at)
              << " state " << (memory.load32(at + 0xcu) & 0xffu) << "); activating again" << std::dec << std::endl;
    portablekit::hle_extension::call_guest(ctx, kActivate, {id, buffer, 0u},
        [start](auto &active, std::uint32_t) { start(active); });
}

inline void sequence(const psprecomp::GuestMemory &memory, psprecomp::AllegrexContext &ctx,
                     std::uint32_t position, std::vector<std::uint32_t> sounds,
                     std::function<void(psprecomp::AllegrexContext &)> next) {
    if (sounds.empty()) { next(ctx); return; }
    const auto sound = sounds.front();
    sounds.erase(sounds.begin());
    play(memory, ctx, position,
         [&memory, position, sounds = std::move(sounds), next](auto &context) {
             sequence(memory, context, position, sounds, next);
         }, sound);
}

inline bool valid_bank(std::span<const std::uint8_t> bytes, std::uint32_t id, std::size_t limit) {
    if (bytes.size() < 0x40 || bytes.size() > limit) return false;
    auto word = [&bytes](unsigned at) {
        return std::uint32_t(bytes[at]) | std::uint32_t(bytes[at + 1]) << 8u |
               std::uint32_t(bytes[at + 2]) << 16u | std::uint32_t(bytes[at + 3]) << 24u;
    };
    const auto end = bytes.size() - 4;
    return word(0) == (0x5053u | id << 16u) &&
           word(4) == bytes.size() && (word(end) == (0x5045u | id << 16u));
}

inline bool valid_foley(std::span<const std::uint8_t> bytes) {
    if (!valid_bank(bytes, fulton_audio_catalog::kBank, fulton_audio_catalog::kCapacity) || bytes.size() < 0xd0)
        return false;
    auto word = [&](std::size_t at) {
        return std::uint32_t(bytes[at]) | std::uint32_t(bytes[at + 1]) << 8 |
               std::uint32_t(bytes[at + 2]) << 16 | std::uint32_t(bytes[at + 3]) << 24;
    };
    auto within = [&](std::uint64_t at, std::uint64_t size) {
        return at <= bytes.size() - 4 && size <= bytes.size() - 4 - at;
    };
    if (word(8) != 0x30 || word(12) != 0xb0 || word(16) != 0xd0) return false;
    for (unsigned i = 0; i < 4; ++i) {
        if (word(0x30 + i * 32 + 8) != i * 8) return false;
        const std::uint64_t sample = 0xd0ull + word(0xb0 + i * 8);
        const std::uint64_t instrument = std::uint64_t(word(0x18)) + word(0xb0 + i * 8 + 4);
        if (!within(sample, 8) || !within(instrument, 24)) return false;
        const auto size = word(sample);
        if (!size || size % 16 || !within(sample + 8, size)) return false;
    }
    return true;
}

inline void install(psprecomp::Runtime &) {
    if (!fulton_assets::enabled()) return;
    const auto directory = fulton_assets::directory();
    auto load = [directory](const char *name, std::uint32_t id, std::size_t limit) {
        std::ifstream file(std::filesystem::path(directory) / name, std::ios::binary);
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
        if (!bytes.empty() && !valid_bank(bytes, id, limit)) {
            std::cerr << "[fulton] " << name << " failed bank id/size/footer checks" << std::endl;
            bytes.clear();
        }
        return bytes;
    };
    bank = load("fulton.sep", 0x31u, 0x8000u);
    lift_bank = load("fulton-lift.sep", 0x32u, 0x11000u);
    foley_bank = load("fulton-foley.sep", fulton_audio_catalog::kBank, fulton_audio_catalog::kCapacity);
    if (!foley_bank.empty() && !sequenced(foley_bank) && !valid_foley(foley_bank)) {
        std::cerr << "[fulton] fulton-foley.sep has invalid cue/sample tables" << std::endl;
        foley_bank.clear();
    }
}
} // namespace mgs::fulton_sound
