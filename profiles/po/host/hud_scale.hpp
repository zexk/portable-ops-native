#pragma once

// Interface size: the gameplay HUD scaled towards the screen's edges.
//
// Each LA2 layout object (0x120 bytes) carries a tuple at +0x114:
// (tx, ty) in 1/16 pixel, a 12-bit angle and an 8.8 scale, applied by the
// engine to the whole layout when dirty flag 0x100000 at +0xC is set
// (setter 0x19FEF8, consumer 0x19D3DC). Scaling by k about PSP pixel (ax, ay)
// is scale = k * 0x100 and t = 16 * a * (1 - k).
//
// HUD layouts are recognised when they are made: the layout actor factories
// 0xA9F18/0xAA008 (hash in a0, actor handle back, layout at actor +0x40) and
// the constructor wrapper 0x196C70 (hash in a0, layout back). These are called
// from other generated units, so wrapping them sees every call. Once a frame
// the recorded layouts get the tuple for the current setting; a record whose
// LA2 file pointer (+0x10) changed belongs to a freed layout and is dropped.
// The character name beside the life gauge is drawn outside the layouts and
// keeps its size.

#include "guest_wrap.hpp"
#include "psprecomp/runtime.hpp"
#include "settings/settings.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace mgs::hud_scale {

struct Anchor {
    std::uint32_t hash;  // GV_StrCode of the layout's name, without .la2
    std::int16_t x, y;   // PSP pixel the layout shrinks towards
};

// lay_life_stmn, lay_radar, lay_itemwin, lay_wpnwin, lay_itemchange (the
// centred weapon/item wheel, which shrinks towards the middle).
constexpr Anchor kAnchors[] = {
    {0x81B2CEu, 0, 272}, {0xC14397u, 480, 0}, {0x247E79u, 0, 272}, {0x1DCA51u, 480, 272}, {0x27D75Cu, 240, 136},
};

struct Layout {
    std::uint32_t object, file;
    Anchor anchor;
};

inline std::vector<Layout> layouts;

inline const Anchor *anchor_for(std::uint32_t key) {
    for (const Anchor &a : kAnchors)
        if (a.hash == (key & 0xFFFFFFu)) return &a;
    return nullptr;
}

inline void remember(psprecomp::Runtime &rt, std::uint32_t key, std::uint32_t object) {
    const Anchor *anchor = anchor_for(key);
    if (anchor == nullptr || object < 0x08800000u || object >= 0x0A000000u - 0x120u) return;
    const std::uint32_t file = rt.memory().load32(object + 0x10u);
    std::erase_if(layouts, [&](const Layout &l) { return l.object == object; });
    layouts.push_back({object, file, *anchor});
}

// Handle low word is the actor's address / 16 (0xAA338 checks actor +0x18).
inline void remember_actor(psprecomp::Runtime &rt, std::uint32_t key, std::uint32_t handle) {
    if (anchor_for(key) == nullptr) return;
    // A failed call leaves other values in v0; only user RAM can be an actor.
    const std::uint32_t actor = handle << 4u;
    if (actor < 0x08800000u || actor >= 0x0A000000u - 0x100u) return;
    if (rt.memory().load32(actor + 0x18u) != handle) return;
    remember(rt, key, rt.memory().load32(actor + 0x40u));
}

inline GuestWrap factory, factory2, wrapper;

inline void make_actor(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    const std::uint32_t key = ctx.gpr[4];
    if (factory.inner || anchor_for(key) == nullptr) {
        factory.inner = false;
        factory.original(rt, ctx);
        return;
    }
    factory.around(rt, ctx, [key](psprecomp::Runtime &r, std::uint32_t handle) { remember_actor(r, key, handle); });
}
inline void make_actor2(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    const std::uint32_t key = ctx.gpr[4];
    if (factory2.inner || anchor_for(key) == nullptr) {
        factory2.inner = false;
        factory2.original(rt, ctx);
        return;
    }
    factory2.around(rt, ctx, [key](psprecomp::Runtime &r, std::uint32_t handle) { remember_actor(r, key, handle); });
}
inline void make_layout(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    const std::uint32_t key = ctx.gpr[4];
    if (wrapper.inner || anchor_for(key) == nullptr) {
        wrapper.inner = false;
        wrapper.original(rt, ctx);
        return;
    }
    wrapper.around(rt, ctx, [key](psprecomp::Runtime &r, std::uint32_t object) { remember(r, key, object); });
}

inline void frame(psprecomp::Runtime &rt) {
    auto &memory = rt.memory();
    const float k = static_cast<float>(portablekit::settings::current().hud_scale_percent) / 100.0f;
    std::erase_if(layouts, [&](const Layout &l) { return memory.load32(l.object + 0x10u) != l.file; });
    for (const Layout &l : layouts) {
        const auto tx = static_cast<std::int16_t>(16.0f * l.anchor.x * (1.0f - k));
        const auto ty = static_cast<std::int16_t>(16.0f * l.anchor.y * (1.0f - k));
        const auto scale = static_cast<std::int16_t>(k * 256.0f);
        const std::uint32_t xy = static_cast<std::uint16_t>(tx) | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(ty)) << 16u);
        const std::uint32_t angle_scale = memory.load32(l.object + 0x118u);
        const std::uint32_t want = (angle_scale & 0xFFFFu) | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(scale)) << 16u);
        if (memory.load32(l.object + 0x114u) == xy && angle_scale == want) continue;
        memory.store32(l.object + 0x114u, xy);
        memory.store32(l.object + 0x118u, want);
        memory.store32(l.object + 0xCu, memory.load32(l.object + 0xCu) | 0x100000u);
    }
}

inline void install(psprecomp::Runtime &runtime, std::uint32_t make_actor_at, std::uint32_t make_actor2_at,
                    std::uint32_t make_layout_at) {
    factory.install(runtime, make_actor_at, &make_actor, "mgs_hud_layout_actor");
    factory2.install(runtime, make_actor2_at, &make_actor2, "mgs_hud_layout_actor2");
    wrapper.install(runtime, make_layout_at, &make_layout, "mgs_hud_layout");
    std::cout << "[hud] interface layouts scaled" << std::endl;
}

} // namespace mgs::hud_scale
