#pragma once

// Game > Difficulty: play at the game's own Easy, Normal or Extreme whatever
// the save was started on, without changing the save (PO ULUS-10202 1.02;
// PO+ is not wired).
//
// The difficulty is link variable 0x48, a word on MGS3's scale: the title's
// new game sets 20, 30 or 50 (Easy, Normal, Extreme; MGS3's Hard, 40, is
// never set). The engine never reads it; stage scripts and stage code do:
// soldiers' sight, hearing and life options, the damage they take, and more
// (docs: feature-hooks.md, Difficulty).
//
// The override is written into the live link bank when a stage other than
// the title loads, and the save's own value is kept aside. Saves are made
// from the snapshot banks, not the live ones: after every copy into the
// snapshot (0x3E73C every variable, 0x3F194 one) it gets the save's value
// back, and after a copy out of it (0x3E8D8, as a retry does) the override
// is written again. The title gets the save's value back, so its menus, new
// games and loads see the game's own. The copies are called from other code
// units, so the dispatcher reaches the wraps.

#include "guest_wrap.hpp"
#include "install/user_data.hpp"
#include "portablekit/hle_extension.hpp"
#include "presence.hpp"
#include "psprecomp/runtime.hpp"

#if defined(PORTABLEKIT_HAS_RENDERER)
#include "ui/widgets.hpp"
#endif

#include <cstdint>
#include <exception>
#include <iostream>
#include <string_view>

namespace mgs::difficulty {

struct Addresses {
    std::uint32_t link;          // pointer to the live link bank
    std::uint32_t snapshot;      // the link bank's snapshot
    std::uint32_t snapshot_all;  // live to snapshot, every variable
    std::uint32_t snapshot_one;  // live to snapshot, one variable
    std::uint32_t restore;       // snapshot to live
};
constexpr std::uint32_t kDifficulty = 0x48u;

struct Level {
    const char *name, *key;
    std::uint32_t value;  // 0: the save's own
};
inline constexpr Level kLevels[] = {
    {"Save's own", "save", 0u}, {"Easy", "easy", 20u}, {"Normal", "normal", 30u}, {"Extreme", "extreme", 50u}};

inline Addresses at{};
inline int level = 0;
inline bool active = false;        // the live bank holds the override
inline std::uint32_t real = 0;     // the save's own value, while active
inline std::uint32_t written = 0;  // the override in the live bank, while active
inline GuestWrap snapshot_all, snapshot_one, restore;

inline void load() {
    try {
        const auto entries = portablekit::install::read_settings_file(portablekit::install::user_data_directory());
        if (const auto found = entries.find("game.difficulty"); found != entries.end())
            for (int i = 0; i < 4; ++i)
                if (found->second == kLevels[i].key) level = i;
    } catch (const std::exception &) {
    }
    // <prefix>_DIFFICULTY (0 to 3) decides for one run.
    if (const char *text = portablekit::hle_extension::env("DIFFICULTY"); text && *text >= '0' && *text <= '3')
        level = *text - '0';
}

inline void save() {
    try {
        const auto root = portablekit::install::user_data_directory();
        auto entries = portablekit::install::read_settings_file(root);
        entries["game.difficulty"] = kLevels[level].key;
        portablekit::install::write_settings_file(root, entries);
    } catch (const std::exception &error) {
        std::cerr << "[difficulty] cannot save setting: " << error.what() << std::endl;
    }
}

// Writes the override, keeping the save's value aside: anything else found
// in the live bank is the game's own (a new game, a load, a retry).
inline void apply(psprecomp::GuestMemory &memory) {
    const std::uint32_t bank = memory.load32(at.link);
    if (!memory.contains(bank + kDifficulty, 4u)) return;
    const std::uint32_t where = bank + kDifficulty, value = kLevels[level].value;
    // A game starts at the title; the stages before it hold the engine's default.
    static bool titled = false;
    if (presence::state.stage == "title") titled = true;
    if (value == 0u || !titled || presence::state.stage == "title") {
        if (active && memory.load32(where) == written) memory.store32(where, real);
        active = false;
        return;
    }
    if (!active || memory.load32(where) != written) {
        const std::uint32_t own = memory.load32(where);
        if (own < 20u || own > 50u) {  // no game yet (the stages before the title)
            active = false;
            return;
        }
        real = own;
    }
    memory.store32(where, value);
    if (!active || written != value)
        std::cout << "[difficulty] " << kLevels[level].name << " over the save's " << real << std::endl;
    written = value;
    active = true;
}

inline void keep_snapshot(psprecomp::Runtime &runtime, std::uint32_t) {
    auto &memory = runtime.memory();
    if (active && memory.load32(at.snapshot + kDifficulty) == written)
        memory.store32(at.snapshot + kDifficulty, real);
}

inline void wrap_snapshot_all(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    snapshot_all.around(rt, ctx, keep_snapshot);
}
inline void wrap_snapshot_one(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    snapshot_one.around(rt, ctx, keep_snapshot);
}
inline void wrap_restore(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    restore.around(rt, ctx, [](psprecomp::Runtime &runtime, std::uint32_t) { apply(runtime.memory()); });
}

// The snapshot wraps stay in place whatever the level: the override may be
// switched on later, and must never reach a save.
inline void install(psprecomp::Runtime &runtime, const Addresses &addresses) {
    at = addresses;
    load();
    if (!snapshot_all.install(runtime, at.snapshot_all, &wrap_snapshot_all, "mgs_snapshot_all") ||
        !snapshot_one.install(runtime, at.snapshot_one, &wrap_snapshot_one, "mgs_snapshot_one") ||
        !restore.install(runtime, at.restore, &wrap_restore, "mgs_restore_snapshot")) {
        std::cerr << "[difficulty] snapshot copies not found; override off" << std::endl;
        at = {};
    }
}

inline void module_ready(psprecomp::Runtime &runtime, std::string_view name) {
    if (at.link != 0u && name == "mgp_stage") apply(runtime.memory());
}

inline void reset() {
    level = 0;
    save();
}

inline void draw() {
#if defined(PORTABLEKIT_HAS_RENDERER)
    using namespace portablekit::ui;
    section("Difficulty");
    if (const int delta = choice_row(
            "Difficulty", kLevels[level].name,
            {at.link == 0u, at.link == 0u ? "Unavailable" : "",
             "Plays at the game's own Easy, Normal or Extreme whatever the save was started on: soldiers' sight, "
             "hearing and toughness follow it. The save keeps its own difficulty. Applies when the next area "
             "loads."})) {
        level = (level + delta + 4) % 4;
        save();
    }
#endif
}

} // namespace mgs::difficulty
