#pragma once

// Frame interpolation limits in the engine's units. Its world is about ten
// times the framework's defaults in size: the third-person camera orbits some
// 3,150 units out, so an ordinary turn or walk moves it 250 to 450 units a game
// frame (past the default 200, which made every such frame a cut and showed it
// unblended), and a moving character steps past the default 120 of its own.

#include "gpu/frame_interpolation.hpp"

namespace mgs {

inline constexpr portablekit::gpu::interpolation::CutThresholds kInterpolationThresholds{
    .max_camera_distance = 2000.0f,
    .continuous_distance_margin = 600.0f,
    .max_continuous_distance = 8000.0f,
    // Rolling moves animated mesh points up to 1,010 units per game frame
    // in PO; 600 rejected individual body parts and held their older poses.
    .max_own_motion = 1200.0f,
    // The engine culls by bounding box, so whatever has its origin on screen
    // was drawn: a draw only the newer frame has there is another model of
    // something the older frame drew (a guard changing its level of detail).
    .bring_edge = 1.0f,
    // The item and weapon windows, menus and markers are through-mode sprites.
    .through = true,
};

} // namespace mgs
