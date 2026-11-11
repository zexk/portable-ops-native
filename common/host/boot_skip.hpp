#pragma once

// Game > Startup: skip the logo screens and the opening movie (PO ULUS-10202
// 1.02). Both are choices of the title stage's own scenario, steered at the
// engine's procedure call 0x3DF48 (a0 = procedure number), so nothing on the
// disc and no stage code changes.
//
// The title scenario, after the Memory Stick checks, runs procedure 228:
//   if (var 0x120 != 0) 232 else 230
// 230 creates the logo actor (chara 0x71462A), which calls 232 when the logos
// are over or cancelled. 232 creates the title actor (chara 0xC5A5D9) with
// option 's' = var 0x120. In title.prx (0x2EBB0) 's' picks the first state
// from a table of six: 0 plays the opening movie (option 'm', MPO_OP_LONG.pmf)
// before the menu, 1 to 3 open the menu at once (2 and 3 on a set page).
// var 0x120 is a word of the global script bank (0x220CD8), nonzero when the
// game comes back to the title, which is how the game itself skips both.
//
// A script's own procedure calls do not pass 0x3DF48 (only calls from code
// do), so 230 cannot be redirected there. Skip logos: the logo actor's
// update (title.prx 0x2F058) runs the state function at work +0x58, first
// 0x2EF78 (first logo), then 0x2EE4C (second), then 0x2EDDC, which calls the
// procedure of option 'n' (232) and ends the actor. The first is replaced
// by the last before it ever runs. Skip the movie: var 0x120
// is set to 1 when 232 is called with it still 0.

#include "portablekit/hle_extension.hpp"
#include "install/user_data.hpp"
#include "presence.hpp"
#include "psprecomp/runtime.hpp"

#if defined(PORTABLEKIT_HAS_RENDERER)
#include "ui/widgets.hpp"
#endif

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <exception>
#include <iostream>

namespace mgs::boot_skip {
// The numbers above are PO's. PO+ has the same scenario and logo actor under
// other numbers (US 1.01, EU 1.00, JP 1.03, read from each disc's title
// scenario and title.prx): the title procedure is 103 (EU 104), the word is
// var 0x3E4, and the engine holds a pointer to the bank, not the bank.
struct Addresses {
    std::uint32_t call_procedure;  // guest address of the engine's procedure call
    std::uint32_t bank;            // the global script bank, or the pointer to it
    bool bank_is_pointer;
    std::uint32_t returned;        // the bank word that is nonzero on a return to the title
    std::uint32_t title;           // the procedure that creates the title actor
    // title.prx: the logo actor's update and two of its state functions.
    std::uint32_t logo_update, logo_first, logo_done;
};
inline Addresses at{};
constexpr std::array<std::uint32_t, 4> kLogoUpdateShape{0x27bdfff0u, 0xafb00000u, 0xafbf0004u, 0x8c820058u};

inline bool logos = false, movie = false;  // what is skipped, as saved
inline psprecomp::Runtime::RecompiledFunction original_call = nullptr;
inline psprecomp::Runtime::RecompiledFunction original_logo_update = nullptr;
inline std::uint32_t title_base = 0;

inline void save(const char *key, bool &setting, bool value) {
    try {
        const auto root = portablekit::install::user_data_directory();
        auto entries = portablekit::install::read_settings_file(root);
        entries[key] = value ? "1" : "0";
        portablekit::install::write_settings_file(root, entries);
        setting = value;
    } catch (const std::exception &error) {
        std::cerr << "[startup] cannot save setting: " << error.what() << std::endl;
    }
}

inline void load() {
    try {
        const auto entries = portablekit::install::read_settings_file(portablekit::install::user_data_directory());
        const auto on = [&entries](const char *key, const char *env) {
            // <prefix>_SKIP_LOGOS / <prefix>_SKIP_MOVIE (0 or 1) decide for one run.
            if (const char *text = portablekit::hle_extension::env(env)) return *text == '1';
            const auto found = entries.find(key);
            return found != entries.end() && found->second == "1";
        };
        logos = on("startup.skip_logos", "SKIP_LOGOS");
        movie = on("startup.skip_movie", "SKIP_MOVIE");
    } catch (const std::exception &) {
    }
}

inline void call_procedure(psprecomp::CorpusRuntime &runtime, psprecomp::AllegrexContext &ctx) {
    // <prefix>_TRACE_PROCEDURES: every script procedure called, by stage.
    static const bool trace = portablekit::hle_extension::env("TRACE_PROCEDURES") != nullptr;
    if (trace) std::cout << "[proc] " << presence::state.stage << ' ' << std::dec << ctx.gpr[4] << std::endl;
    if ((logos || movie) && presence::state.stage == "title") {
        auto &memory = static_cast<psprecomp::Runtime &>(runtime).memory();
        const std::uint32_t word = (at.bank_is_pointer ? memory.load32(at.bank) : at.bank) + at.returned;
        if (movie && ctx.gpr[4] == at.title && memory.contains(word, 4u) && memory.load32(word) == 0u) {
            memory.store32(word, 1u);
            std::cout << "[startup] opening movie skipped" << std::endl;
        }
    }
    original_call(runtime, ctx);
}

inline void logo_update(psprecomp::CorpusRuntime &runtime, psprecomp::AllegrexContext &ctx) {
    auto &memory = static_cast<psprecomp::Runtime &>(runtime).memory();
    const std::uint32_t state = ctx.gpr[4] + 0x58u;
    if (logos && presence::state.stage == "title" && memory.contains(state, 4u) &&
        memory.load32(state) == title_base + at.logo_first) {
        memory.store32(state, title_base + at.logo_done);
        std::cout << "[startup] logo screens skipped" << std::endl;
    }
    original_logo_update(runtime, ctx);
}

// The title stage's module is in place: wrap its logo actor's update.
inline void module_ready(psprecomp::Runtime &runtime, std::string_view name, std::uint32_t base,
                         std::span<const std::uint8_t> code) {
    if (name != "mgp_stage" || presence::state.stage != "title" || code.size() < at.logo_update + 16u) return;
    std::array<std::uint32_t, 4> words{};
    std::memcpy(words.data(), code.data() + at.logo_update, sizeof(words));
    const auto original = runtime.registered_function(base + at.logo_update);
    if (words != kLogoUpdateShape || original == nullptr || original == &logo_update) return;
    original_logo_update = original;
    title_base = base;
    runtime.register_function(base + at.logo_update, &logo_update, "mgs_logo_update");
}

inline void install(psprecomp::Runtime &runtime, const Addresses &addresses) {
    at = addresses;
    load();
    const psprecomp::Runtime::RecompiledFunction original = runtime.registered_function(at.call_procedure);
    if (original == nullptr || original == &call_procedure) return;
    original_call = original;
    runtime.register_function(at.call_procedure, &call_procedure, "mgs_call_procedure");
}

inline void reset() {
    save("startup.skip_logos", logos, false);
    save("startup.skip_movie", movie, false);
}

inline void draw() {
#if defined(PORTABLEKIT_HAS_RENDERER)
    using namespace portablekit::ui;
    section("Startup");
    if (toggle_row("Skip logo screens", logos, {false, {}, "Go straight past the company logos when the game starts."}))
        save("startup.skip_logos", logos, !logos);
    if (toggle_row("Skip opening movie", movie,
                   {false, {}, "Open the title menu without playing the opening movie first."}))
        save("startup.skip_movie", movie, !movie);
#endif
}
} // namespace mgs::boot_skip
