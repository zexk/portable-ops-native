#pragma once

// Game > Sound: separate levels for music, effects, voices and cutscenes
// (PO ULUS-10202 1.02; sound2 at 0x08B10100).
//
// sound2 mixes every voice itself and hands the host one stream, so the
// levels are set where the game keeps its own. A voice's level is the
// product of six faders; fader 0 starts at one of three masters at sound2
// 0x2D164, picked by the play id: music (no 0x100000: entries flagged 0x80,
// the reserved stream ids), voices (0x200000: entries flagged 0x20) and
// effects. Export 0xFA32C9B9 sets a master and fader 0 of the voices playing;
// the engine calls it once, at start, for full level. Here the masters and
// each playing voice's fader 0 are written instead, as that export would.
//
// A cutscene's streams (the oss_ banks) carry cue categories 0xC8 speech,
// 0xC9 music, 0xCA effects, where everything else has 0; most of their
// music is not flagged as music. A voice with one of those categories
// follows the Cutscenes level alone.
//
// The voice flag covers streamed speech only. Speech and grunts kept as
// ADPCM (type 3) carry no flag: the characters' sets in banks 56-71 and
// 74-82, the lines beside the soldiers' streamed speech in 72-96, and the
// hit grunts in the three-sound banks up to 119. Nor do the headquarters'
// radio calls, streamed from banks 46 and 47. Those count as voices here by
// bank (heard from banks 46, 56, 76, 90, 98 and 99 on 2026-11-12; the rest
// by their matching layout). Banks 53 and 54 look like 46 but are not
// speech: 53 is the truck that brings the squad to a stage.
//
// Levels are applied on sound2's own thread, at the semaphore wait that
// opens each block of its mix loop (0x8C98), before it works out the voices'
// levels (0x763C). All four at 100% leaves the game as it is.

#include "hle/hle_common.hpp"
#include "install/user_data.hpp"
#include "psprecomp/runtime.hpp"

#if defined(PORTABLEKIT_HAS_RENDERER)
#include "ui/widgets.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

namespace mgs::sound_levels {
constexpr std::uint32_t kSound2 = 0x08B10100u;
constexpr std::uint32_t kCatalogue = kSound2 + 0x2D340u;  // pointer to the SHDS table
constexpr std::uint32_t kBankIds = kSound2 + 0x2D360u;    // how many bank ids it has
constexpr std::uint32_t kMasters = kSound2 + 0x2D164u;    // music, effects, voices
constexpr std::uint32_t kVoices = kSound2 + 0x287A0u, kVoiceSize = 0x214u, kVoiceCount = 0x22u;
constexpr std::uint32_t kMixLoopWait = kSound2 + 0x8CA0u;  // where the loop's first wait returns
constexpr std::uint32_t kUnity = 0x1000u;

enum : std::size_t { kMusic, kEffects, kVoice, kCutscenes, kLevels };
constexpr std::array<const char *, kLevels> kKeys{"sound.music", "sound.effects", "sound.voices", "sound.cutscenes"};
inline std::array<int, kLevels> percent{100, 100, 100, 100};
inline bool loaded = false;
inline bool applied = false;  // the game's levels are not its own right now

inline void load() {
    if (loaded) return;
    loaded = true;
    try {
        const auto entries = portablekit::install::read_settings_file(portablekit::install::user_data_directory());
        for (std::size_t i = 0; i < kLevels; ++i)
            if (const auto found = entries.find(kKeys[i]); found != entries.end())
                percent[i] = std::clamp(std::stoi(found->second), 0, 100);
    } catch (const std::exception &) {
    }
}

inline void save() {
    try {
        const auto root = portablekit::install::user_data_directory();
        auto entries = portablekit::install::read_settings_file(root);
        for (std::size_t i = 0; i < kLevels; ++i) entries[kKeys[i]] = std::to_string(percent[i]);
        portablekit::install::write_settings_file(root, entries);
    } catch (const std::exception &error) {
        std::cerr << "[sound] cannot save setting: " << error.what() << std::endl;
    }
}

// A sound's bank id and the category of its cue, as sound2 0x3B8 finds it;
// zeros without a bank.
struct Source {
    std::uint32_t bank = 0u, category = 0u;
};
inline Source source(const psprecomp::GuestMemory &memory, std::uint32_t catalogue, std::uint32_t sound) {
    const std::uint32_t firsts = memory.load32(catalogue + 0x20u), banks = memory.load32(catalogue + 0x18u);
    const std::uint32_t count = std::min<std::uint32_t>(memory.load32(kBankIds), 0x1000u);
    if (!memory.contains(firsts, count * 2u) || !memory.contains(banks, count * 16u)) return {};
    std::uint32_t index = 0u;
    while (index < count && memory.load16(firsts + index * 2u) <= sound) ++index;
    if (index == 0u) return {};
    --index;
    const std::uint32_t bank = memory.load32(banks + index * 16u);
    if (bank == 0u || !memory.contains(bank, 12u)) return {index, 0u};
    const std::uint32_t cue = bank + memory.load32(bank + 8u) + (sound - memory.load16(firsts + index * 2u)) * 0x20u;
    return {index, memory.contains(cue, 0x20u) ? memory.load8(cue + 4u) : 0u};
}

inline void apply(psprecomp::GuestMemory &memory) {
    load();
    const bool wanted = std::any_of(percent.begin(), percent.end(), [](int value) { return value != 100; });
    if (!wanted && !applied) return;
    if (!memory.contains(kCatalogue, 0x24u)) return;
    const std::uint32_t catalogue = memory.load32(kCatalogue);
    if (!memory.contains(catalogue, 0x34u) || memory.load32(catalogue) != 0x53444853u) return;  // "SHDS"
    applied = wanted;

    const auto level = [](std::size_t which) { return kUnity * static_cast<std::uint32_t>(percent[which]) / 100u; };
    for (std::size_t i = 0; i < 3u; ++i) memory.store16(kMasters + static_cast<std::uint32_t>(i) * 2u, static_cast<std::uint16_t>(level(i)));

    // Where the sound each voice plays comes from, looked up once a sound.
    static std::array<std::uint32_t, kVoiceCount> play_of{};
    static std::array<Source, kVoiceCount> source_of{};
    for (std::uint32_t slot = 0; slot < kVoiceCount; ++slot) {
        const std::uint32_t voice = kVoices + slot * kVoiceSize;
        if ((memory.load8(voice + 0x30u) & 0x80u) != 0u) continue;  // free
        const std::uint32_t play = memory.load32(voice + 0x1Cu);
        std::size_t which = (play & 0x100000u) == 0u ? kMusic : (play & 0x200000u) != 0u ? kVoice : kEffects;
        if (play_of[slot] != play) {
            play_of[slot] = play;
            source_of[slot] = source(memory, catalogue, play & 0xFFFFu);
        }
        const std::uint32_t bank = source_of[slot].bank, kind = play >> 16 & 0xFu;
        if (kind == 3u && bank >= 56u && bank <= 119u) which = kVoice;
        if (kind == 5u && (bank == 46u || bank == 47u)) which = kVoice;
        if (source_of[slot].category >= 0xC8u && source_of[slot].category <= 0xCAu) which = kCutscenes;
        // Fader 0: target, current, fraction, step, steps left. Flag 0x200
        // has 0x763C work the voice's level out again.
        const std::uint32_t fader = voice + 0x50u;
        const auto want = static_cast<std::uint16_t>(level(which));
        if (memory.load16(fader) == want && memory.load16(fader + 2u) == want) continue;
        memory.store16(fader, want);
        memory.store16(fader + 2u, want);
        memory.store8(fader + 4u, 0u);
        memory.store16(fader + 6u, 0u);
        memory.store16(fader + 8u, 0u);
        memory.store16(voice + 0x28u, static_cast<std::uint16_t>(memory.load16(voice + 0x28u) | 0x200u));
    }
}

// sceKernelWaitSema, with the levels applied when sound2's mix loop calls it.
inline void install(portablekit::HleRegistrar &hle) {
    const std::string library = "ThreadManForUser";
    constexpr std::uint32_t kWaitSema = 0x4E3A1105u;
    const portablekit::HleFunction previous = hle.bound_function(library, kWaitSema);
    if (!previous) {
        std::cerr << "[sound] sceKernelWaitSema is not bound; sound levels are off" << std::endl;
        return;
    }
    hle.bind(library, kWaitSema, [previous](portablekit::Runtime &runtime, portablekit::AllegrexContext &ctx) {
        if (ctx.gpr[31] == kMixLoopWait) apply(runtime.memory());
        previous(runtime, ctx);
    });
}

inline void reset() {
    percent.fill(100);
    save();
}

inline void draw() {
#if defined(PORTABLEKIT_HAS_RENDERER)
    using namespace portablekit::ui;
    load();
    section("Sound");
    constexpr std::array<const char *, kLevels> labels{"Music", "Effects", "Voices", "Cutscenes"};
    constexpr std::array<const char *, kLevels> help{
        "Music during missions and in menus.",
        "Footsteps, weapons, alarms and menu sounds.",
        "Speech, radio calls and grunts during missions.",
        "Everything a cutscene plays: its speech, music and effects.",
    };
    for (std::size_t i = 0; i < kLevels; ++i)
        if (slider_row(labels[i], percent[i], 0, 100, 5, "%d%%", {false, {}, help[i]})) save();
#endif
}
} // namespace mgs::sound_levels
