#pragma once

// What a new player starts with: the game as it plays on a PSP, drawn with
// what a PC has. Only the picture and its smoothness change; gameplay
// features stay off until the player turns them on. settings.ini keeps a
// player's own choices, so this reaches only new players and reset settings.

#include "settings/settings.hpp"

namespace mgs {

inline void settings_defaults(portablekit::settings::Settings &values) {
    using namespace portablekit::settings;
    values.fullscreen = true;
    values.internal_scale = 0u;  // the window's own size
    values.aspect = Aspect::Fill;
    values.msaa = 4u;
    values.anisotropy = 16u;
    values.detail_distance_percent = 200u;
    values.frame_rate = FrameRate::Display;
    values.interpolation_lead_percent = 100u;
}

} // namespace mgs
