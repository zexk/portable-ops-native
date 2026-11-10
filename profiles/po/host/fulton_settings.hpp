#pragma once

#include "fulton_assets.hpp"

#if defined(PORTABLEKIT_HAS_RENDERER)
#include "ui/file_browser.hpp"
#include "ui/widgets.hpp"
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
}

#if defined(PORTABLEKIT_HAS_RENDERER)
inline std::unique_ptr<portablekit::ui::FileBrowser> browser;
#endif

// The folder browser of "Import Fulton assets…". True while it is open.
inline bool screen([[maybe_unused]] bool back) {
#if defined(PORTABLEKIT_HAS_RENDERER)
    using portablekit::ui::FileBrowser;
    if (!browser) return false;
    const auto result = browser->frame(back);
    if (result == FileBrowser::Result::Chosen) {
        try {
            fulton_assets::import_bundle(browser->chosen());
            // The running game keeps its startup snapshot; only the menu learns.
            fulton_assets::available = true;
            fulton_assets::status = "Assets imported";
        } catch (const std::exception &error) {
            fulton_assets::status = "Import failed: not a converted Fulton folder";
            std::cerr << "[fulton] import failed: " << error.what() << std::endl;
        }
    }
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
    section("Peace Walker");
    RowOptions options;
    options.disabled = override || (!fulton_assets::available && !fulton_assets::selected);
    options.note = override ? "Developer override" : fulton_assets::status;
    options.description = "Recover unconscious or prone held-up soldiers with Triangle while beside them. "
                          "Applies after restarting the game.";
    if (toggle_row("Fulton recovery (experimental)", fulton_assets::selected, options))
        save(!fulton_assets::selected);
    if (!override && !fulton_assets::available &&
        button_row("Import Fulton assets…",
                   {false, {}, "Choose a folder of converted Fulton assets, the one that holds fult_enemy.mtar. "
                               "They are checked and copied into the game's own data; the folder is not "
                               "needed afterwards. Importing does not turn Fulton on."})) {
        FileBrowser::Options browse;
        browse.extensions = {};
        browse.filter_name = browse.listed_name = "folders";
        browse.empty_note = "No folders here.";
        browse.choose_folder = "Import from this folder";
        browse.choose_on_open = [](const std::filesystem::path &folder) {
            std::error_code error;
            return std::filesystem::is_regular_file(folder / "fult_enemy.mtar", error);
        };
        browser = std::make_unique<FileBrowser>(FileBrowser::home(), std::move(browse));
    }
    if (!override && fulton_assets::selected != fulton_assets::enabled())
        info_row("Fulton", fulton_assets::available ? "Restart to apply" : "Inactive: assets required");
#endif
}
} // namespace mgs::fulton_settings
