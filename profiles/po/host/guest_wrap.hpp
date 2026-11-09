#pragma once

// Running code after a wrapped guest function returns.
//
// A registered override receives the recompiled unit for its address; calling
// that unit returns as soon as control leaves it (an import, a call into an
// unchained unit), not when the guest function returns. To see the result,
// the wrapper re-enters its own address through call_guest, whose return stub
// runs `after` with v0 once the function has really returned. The re-entry is
// recognised by a flag and runs the original unit unchanged.

#include "portablekit/hle_extension.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <functional>

namespace mgs {

struct GuestWrap {
    std::uint32_t address = 0;
    psprecomp::Runtime::RecompiledFunction original = nullptr;
    bool inner = false;

    // Returns false (and leaves the hook unset) when nothing is registered there.
    bool install(psprecomp::Runtime &runtime, std::uint32_t at, psprecomp::Runtime::RecompiledFunction hook,
                 const char *name) {
        const psprecomp::Runtime::RecompiledFunction found = runtime.registered_function(at);
        if (found == nullptr || found == hook) return false;
        address = at;
        original = found;
        runtime.register_function(at, hook, name);
        return true;
    }

    // Call from the hook. `after` runs with the guest's result once it returns.
    void around(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx,
                std::function<void(psprecomp::Runtime &, std::uint32_t result)> after) {
        if (inner) {
            inner = false;
            original(rt, ctx);
            return;
        }
        auto &runtime = static_cast<psprecomp::Runtime &>(rt);
        inner = true;
        portablekit::hle_extension::call_guest(
            ctx, address, {ctx.gpr[4], ctx.gpr[5], ctx.gpr[6], ctx.gpr[7]},
            [&runtime, after = std::move(after)](auto &, std::uint32_t result) { after(runtime, result); });
    }
};

} // namespace mgs
