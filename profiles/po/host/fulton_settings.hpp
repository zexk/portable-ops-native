#pragma once

#include "fulton_assets.hpp"

#if defined(PORTABLEKIT_HAS_RENDERER)
#include "ui/widgets.hpp"
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

inline void draw() {
#if defined(PORTABLEKIT_HAS_RENDERER)
    using namespace portablekit::ui;
    const bool override = portablekit::hle_extension::env("FULTON") != nullptr;
    section("Peace Walker");
    RowOptions options;
    options.disabled = override || (!fulton_assets::available && !fulton_assets::selected);
    options.note = override ? "Developer override" : fulton_assets::status;
    options.description = "Recover unconscious or prone held-up soldiers with Triangle while beside them. "
                          "Applies after restarting the game. Works only in prepared stages; "
                          "unsupported stages leave the soldier in place. "
                          "Asset import is not available in this build's menu; see the Fulton setup in README.";
    if (toggle_row("Fulton recovery (experimental)", fulton_assets::selected, options))
        save(!fulton_assets::selected);
    if (!override && fulton_assets::selected != fulton_assets::enabled())
        info_row("Fulton", fulton_assets::available ? "Restart to apply" : "Inactive: assets required");
#endif
}
} // namespace mgs::fulton_settings
