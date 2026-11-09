#pragma once

// Temporary: <prefix>_TRACE_MODELS logs every change of what an object draws
// (model, part masks, flags) with its distance from the camera, to tell
// whether the engine switches levels of detail.

#include "psprecomp/runtime.hpp"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <unordered_map>

namespace mgs {

struct ModelTraceState {
    psprecomp::Runtime::RecompiledFunction skinned = nullptr;
    psprecomp::Runtime::RecompiledFunction rigid = nullptr;
    struct Seen {
        std::uint32_t model, mask0, mask1, flags, parts;
    };
    std::unordered_map<std::uint32_t, Seen> seen;
    std::uint64_t calls = 0;
};
inline ModelTraceState model_trace;

inline void trace_model(psprecomp::Runtime &rt, std::uint32_t object, std::uint32_t view, const char *kind) {
    auto &m = rt.memory();
    const auto f = [&](std::uint32_t a) { return std::bit_cast<float>(m.load32(a)); };
    const std::uint32_t model = m.load32(object + 0xa4u);
    const ModelTraceState::Seen now{model, m.load32(object + 0xf0u), m.load32(object + 0xf4u),
                                    m.load32(object + 0x5cu), model != 0u ? m.load32(model + 0xcu) : 0u};
    ++model_trace.calls;
    auto [it, fresh] = model_trace.seen.try_emplace(object, now);
    const auto &was = it->second;
    if (!fresh && was.model == now.model && was.mask0 == now.mask0 && was.mask1 == now.mask1 &&
        was.flags == now.flags && was.parts == now.parts)
        return;
    float x = f(object + 0x40u), y = f(object + 0x44u), z = f(object + 0x48u);
    const std::uint32_t bones = m.load32(object + 0xacu);
    if (bones != 0u) {
        x = f(bones + 0x30u);
        y = f(bones + 0x34u);
        z = f(bones + 0x38u);
    }
    const float dx = x - f(view + 0x30u), dy = y - f(view + 0x34u), dz = z - f(view + 0x38u);
    std::printf("[model] %s %s obj %08x model %08x->%08x parts %u->%u masks %08x%08x->%08x%08x flags %08x->%08x "
                "distance %.0f (call %llu)\n",
                kind, fresh ? "new" : "changed", object, was.model, now.model, was.parts, now.parts, was.mask1,
                was.mask0, now.mask1, now.mask0, was.flags, now.flags, std::sqrt(dx * dx + dy * dy + dz * dz),
                static_cast<unsigned long long>(model_trace.calls));
    it->second = now;
}

inline void traced_skinned(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    trace_model(static_cast<psprecomp::Runtime &>(rt), ctx.gpr[4], ctx.gpr[5], "skinned");
    model_trace.skinned(rt, ctx);
}

inline void traced_rigid(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    trace_model(static_cast<psprecomp::Runtime &>(rt), ctx.gpr[4], ctx.gpr[5], "rigid");
    model_trace.rigid(rt, ctx);
}

inline void install_model_trace(psprecomp::Runtime &runtime, std::uint32_t skinned, std::uint32_t rigid) {
    if (std::getenv("MGSPO_TRACE_MODELS") == nullptr) return;
    model_trace.skinned = runtime.registered_function(skinned);
    model_trace.rigid = runtime.registered_function(rigid);
    if (model_trace.skinned == nullptr || model_trace.rigid == nullptr) return;
    runtime.register_function(skinned, &traced_skinned, "trace_skinned");
    runtime.register_function(rigid, &traced_rigid, "trace_rigid");
    std::printf("[model] tracing object draws\n");
}

} // namespace mgs

namespace mgs {

// Temporary: <prefix>_TRACE_CAMERA logs the camera's state after each update.
inline psprecomp::Runtime::RecompiledFunction traced_camera_update = nullptr;
inline void trace_camera_update(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    auto &m = static_cast<psprecomp::Runtime &>(rt).memory();
    const std::uint32_t cam = ctx.gpr[4];
    traced_camera_update(rt, ctx);
    const auto f = [&](std::uint32_t a) { return std::bit_cast<float>(m.load32(a)); };
    const auto h = [&](std::uint32_t a) { return static_cast<int>(static_cast<std::int16_t>(m.load16(a))); };
    static std::uint64_t n = 0;
    std::printf("[cam] %llu mode %u flags %x frames %u yaw328 %d 32c %d 318 %.3f 33c %d 300 %d move %d eye %.0f %.0f "
                "%.0f at %.0f %.0f %.0f stick %d %d\n",
                static_cast<unsigned long long>(n++), m.load32(cam + 0x308u), m.load32(cam + 0x310u),
                m.load32(cam + 0x314u), h(cam + 0x328u), h(cam + 0x32cu), f(cam + 0x318u), h(cam + 0x33cu),
                h(cam + 0x300u), h(0x08804000u + 0x2753D8u + 0x26u), f(cam + 0x370u), f(cam + 0x374u),
                f(cam + 0x378u), f(cam + 0x380u), f(cam + 0x384u), f(cam + 0x388u),
                static_cast<int>(m.load32(cam + 0x2f0u)), static_cast<int>(m.load32(cam + 0x2f4u)));
}
inline void install_camera_trace(psprecomp::Runtime &runtime, std::uint32_t update) {
    if (std::getenv("MGSPO_TRACE_CAMERA") == nullptr) return;
    traced_camera_update = runtime.registered_function(update);
    if (traced_camera_update != nullptr) runtime.register_function(update, &trace_camera_update, "trace_camera");
}

} // namespace mgs
