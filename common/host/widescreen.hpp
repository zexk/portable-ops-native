#pragma once

// The engine's 3D view at the window's shape (Aspect: Fill).
//
// Read from the executable: one routine builds every view's matrices from the
// camera, a focal length (f12, 2 normally, more when zoomed) and the view's
// viewport (+0x1b0 width, +0x1b4 height, ints). Each projection is
// x = focal * k and y = focal * width / height, with k a 1.0 the routine loads
// from the executable's data; the routine stores the focal length at +0x1c8
// and builds the clip matrices the object cull tests against from the same
// projections. So the view is reshaped by giving the routine another k, and
// for a narrower picture a shorter focal length, around the call: what is
// drawn and what is culled both widen. Only full-screen views (480x272) are
// reshaped; the focal length the game reads back is left as it set it.

#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iostream>

namespace mgs {

struct WidescreenAddresses {
    std::uint32_t build_view;  // the view builder, called with the view in a0 and the focal length in f12
    std::uint32_t x_scale;     // the 1.0 it loads as k
};

struct WidescreenState {
    WidescreenAddresses addresses;
    psprecomp::Runtime::RecompiledFunction build_view = nullptr;
    // Width over height of the picture the game is drawn into.
    float aspect = 480.0f / 272.0f;
};
inline WidescreenState widescreen_state;

inline void build_view(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    auto &memory = static_cast<psprecomp::Runtime &>(rt).memory();
    const std::uint32_t view = ctx.gpr[4];
    constexpr float kPspAspect = 480.0f / 272.0f;
    const float aspect = widescreen_state.aspect;
    if (aspect == kPspAspect || memory.load32(view + 0x1b0u) != 480u || memory.load32(view + 0x1b4u) != 272u) {
        widescreen_state.build_view(rt, ctx);
        return;
    }
    // Wider: the same height, more to the sides. Narrower: the same width,
    // more above and below.
    const std::uint32_t k = widescreen_state.addresses.x_scale;
    const std::uint32_t one = memory.load32(k);
    const float focal = ctx.fpr[12];
    memory.store32(k, std::bit_cast<std::uint32_t>(std::bit_cast<float>(one) * kPspAspect / aspect));
    ctx.fpr[12] = focal * std::min(1.0f, aspect / kPspAspect);
    widescreen_state.build_view(rt, ctx);
    memory.store32(k, one);
    memory.store32(view + 0x1c8u, std::bit_cast<std::uint32_t>(focal));
}

inline void view_aspect_frame(psprecomp::Runtime &, float aspect) { widescreen_state.aspect = aspect; }

inline void install_widescreen(psprecomp::Runtime &runtime, WidescreenAddresses addresses) {
    const psprecomp::Runtime::RecompiledFunction original = runtime.registered_function(addresses.build_view);
    if (original == nullptr || original == &build_view) return;
    widescreen_state.addresses = addresses;
    widescreen_state.build_view = original;
    runtime.register_function(addresses.build_view, &build_view, "mgs_build_view");
    std::cout << "[widescreen] views reshaped at 0x" << std::hex << addresses.build_view << std::dec << std::endl;
}

} // namespace mgs
