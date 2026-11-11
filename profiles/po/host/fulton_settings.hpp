#pragma once

#include "fulton_assets.hpp"
#include "fulton_convert.hpp"
#include "crouchwalking.hpp"

#if defined(PORTABLEKIT_HAS_RENDERER)
#include "install/installer.hpp"
#include "ui/file_browser.hpp"
#include "ui/widgets.hpp"
#include <chrono>
#include <future>
#include <memory>
#endif

namespace mgs::fulton_settings {
inline void save(bool selected) {
    try {
        const auto root = portablekit::install::user_data_directory();
        auto entries = portablekit::install::read_settings_file(root);
        entries["pw.fulton"] = selected ? "1" : "0";
        portablekit::install::write_settings_file(root, entries);
        fulton_assets::selected = selected;
    } catch (const std::exception &error) {
        fulton_assets::status = "Could not save Fulton setting";
        std::cerr << "[fulton] cannot save setting: " << error.what() << std::endl;
    }
}

inline void reset() {
    if (!portablekit::hle_extension::env("FULTON")) save(false);
    crouchwalking::save(false);
}

#if defined(PORTABLEKIT_HAS_RENDERER)
inline std::unique_ptr<portablekit::ui::FileBrowser> browser;
// A running import: reading a disc image takes seconds, so it works beside
// the menu and reports its outcome as the row's note. Empty on success.
inline std::future<std::string> importing;
inline bool importing_crouchwalk = false;
#endif

// Accept either PW edition, or an already converted bundle. Sources remain
// read-only and are not needed afterwards.
inline std::string import(const std::filesystem::path &chosen) {
    try {
        std::error_code error;
        if (std::filesystem::is_directory(chosen, error)) {
            if (std::filesystem::is_regular_file(chosen / "fult_enemy.mtar", error))
                fulton_assets::import_bundle(chosen);
            else fulton_assets::install_files(fulton_convert::bundle(pw_slot::master_collection(chosen)));
        }
        else {
            portablekit::IsoImage iso(chosen);
            fulton_assets::install_files(fulton_convert::bundle(pw_slot::psp(iso)));
        }
        return {};
    } catch (const std::exception &error) {
        std::cerr << "[fulton] import failed: " << error.what() << std::endl;
        return error.what();
    }
}

// The folder browser of "Import Fulton assets…". True while it is open.
inline bool screen([[maybe_unused]] bool back) {
#if defined(PORTABLEKIT_HAS_RENDERER)
    using portablekit::ui::FileBrowser;
    if (!browser) return false;
    const auto result = browser->frame(back);
    if (result == FileBrowser::Result::Chosen)
        importing = std::async(std::launch::async,
                               importing_crouchwalk ? crouchwalking::import : import, browser->chosen());
    if (result != FileBrowser::Result::Browsing) browser.reset();
    return true;
#else
    return false;
#endif
}

inline void draw() {
#if defined(PORTABLEKIT_HAS_RENDERER)
    using namespace portablekit::ui;
    const bool override = portablekit::hle_extension::env("FULTON") != nullptr;
    const bool busy = importing.valid();
    if (busy && importing.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const auto failure = importing.get();
        // The running game keeps its startup snapshot; only the menu learns.
        if (importing_crouchwalk) {
            crouchwalking::refresh();
            crouchwalking::status = failure.empty() ? "Assets imported" : "Import failed: " + failure;
        } else {
            if (failure.empty()) fulton_assets::available = true;
            fulton_assets::status = failure.empty() ? "Assets imported" : "Import failed: " + failure;
        }
    } else if (busy) {
        if (importing_crouchwalk) crouchwalking::status = "Importing…";
        else fulton_assets::status = "Importing…";
    }
    section("Peace Walker");
    RowOptions options;
    options.disabled = override || (!fulton_assets::available && !fulton_assets::selected);
    options.note = override ? "Developer override" : fulton_assets::status;
    options.description = "Recover unconscious or prone held-up soldiers with Triangle while beside them. "
                          "Applies after restarting the game.";
    if (toggle_row("Fulton recovery (experimental)", fulton_assets::selected, options))
        save(!fulton_assets::selected);
    if (!override && !fulton_assets::available && !importing.valid() &&
        button_row("Import Fulton assets…",
                   {false, {}, "Choose your Peace Walker PSP disc image (.iso), Master Collection installation "
                               "folder, or already converted Fulton assets. Files are converted and copied into "
                               "the game's own data; the source is not changed or needed afterwards. Importing does not "
                               "turn Fulton on."})) {
        FileBrowser::Options browse;
        importing_crouchwalk = false;
        browse.empty_note = "No folders or disc images here.";
        browse.choose_folder = "Import from this folder";
        browse.choose_on_open = [](const std::filesystem::path &folder) {
            std::error_code error;
            return std::filesystem::is_regular_file(folder / "fult_enemy.mtar", error);
        };
        browser = std::make_unique<FileBrowser>(FileBrowser::home(), std::move(browse));
    }
    RowOptions crouch;
    crouch.disabled = !crouchwalking::available && !crouchwalking::selected;
    crouch.note = crouchwalking::status;
    crouch.description = "Move while crouched; hold Triangle for slower stalking. Experimental. "
                         "Applies after restarting the game.";
    if (toggle_row("Crouchwalking (experimental)", crouchwalking::selected, crouch))
        crouchwalking::save(!crouchwalking::selected);
    if (!crouchwalking::available && !importing.valid() &&
        button_row("Import crouchwalk assets…", {false, {},
                   "Choose your Peace Walker PSP disc image (.iso), Master Collection installation folder, "
                   "or converted crouchwalk folder. Importing leaves crouchwalking off."})) {
        importing_crouchwalk = true;
        FileBrowser::Options browse;
        browse.empty_note = "No folders or disc images here.";
        browse.choose_folder = "Import from this folder";
        browse.choose_on_open = [](const std::filesystem::path &folder) {
            return std::filesystem::is_regular_file(folder / "crouchwalk.mtar");
        };
        browser = std::make_unique<FileBrowser>(FileBrowser::home(), std::move(browse));
    }
    // The game takes both at start, so one restart applies either. Without
    // assets there is nothing to apply; the feature's note says what is missing.
    const bool fulton_waits =
        !override && fulton_assets::available && fulton_assets::selected != fulton_assets::enabled();
    const bool crouch_waits = crouchwalking::available && crouchwalking::selected != crouchwalking::enabled;
    if ((fulton_waits || crouch_waits) &&
        button_row("Restart to apply",
                   {false, {}, "Closes the game and starts it again at the title screen. Progress since your last "
                               "save is lost."},
                   colors::kAccentBright))
        portablekit::install::request_restart_on_exit();
#endif
}
} // namespace mgs::fulton_settings
