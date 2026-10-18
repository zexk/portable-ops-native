// <prefix>_SWEEP_STAGE=<name>: loads the stage stage/<name>/ the way the
// engine does, so that it is dumped for recompilation (with
// <prefix>_DUMP_MODULES) without being played to, then stops the run.
//
// A stage is identified by its code after the engine has patched it with
// relocations that come from the stage's _zar archive. So the sweep drives
// the engine's own stage loader: once the game has unloaded a stage (the
// stage slot is then free), it creates a loader for <name> and steps it,
// sleeping between steps while the file system reads, until it has started
// the stage. The stage's first run after the patch is dumped and skipped
// (set_skip_module_first_run), so the stage itself never runs.
#pragma once

#include "hle/hle_common.hpp"
#include "hle/user_modules.hpp"
#include "kernel/kernel.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace mgs {

struct StageLoader {
    std::uint32_t create; // (const char *name) -> loader, after reading the stage's data.cnf
    std::uint32_t step;   // (loader) -> 0 once the stage has been started
    std::uint32_t delay;  // a sceKernelDelayThread(CB) import stub
};

namespace detail {

inline void sweep_step(const StageLoader &loader, psprecomp::AllegrexContext &ctx, std::uint32_t object,
                       std::uint32_t uid) {
    portablekit::kernel().call_guest(ctx, loader.step, {object},
                                     [&loader, object, uid](psprecomp::AllegrexContext &ctx, std::uint32_t more) {
                                         if (more == 0u) {
                                             portablekit::set_skip_module_first_run(false);
                                             portablekit::kernel().runtime().stop("stage sweep done");
                                             portablekit::kernel().finish(ctx, uid);
                                             return;
                                         }
                                         portablekit::kernel().call_guest(
                                             ctx, loader.delay, {1000u},
                                             [&loader, object, uid](psprecomp::AllegrexContext &ctx, std::uint32_t) {
                                                 sweep_step(loader, ctx, object, uid);
                                             });
                                     });
}

} // namespace detail

// The first time the game unloads a stage, the sweep is due; it starts at
// the game's next wait for a vblank, where no file operation is under way
// (the unload itself runs inside one, holding kjfs's open semaphore).
inline void register_stage_sweep(portablekit::HleRegistrar &hle, const StageLoader &loader) {
    if (portablekit::env("SWEEP_STAGE") == nullptr) return;
    static bool due = false;
    hle.add("ModuleMgrForUser", "sceKernelUnloadModule", [](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
        const std::uint32_t uid = portablekit::arg(ctx, 0);
        if (portablekit::unload_user_module(rt, uid)) due = true;
        portablekit::kernel().finish(ctx, uid);
    });
    constexpr std::uint32_t kWaitVblankStartCB = 0x46F186C3u;
    const auto wait = hle.bound_function("sceDisplay", kWaitVblankStartCB);
    hle.bind("sceDisplay", kWaitVblankStartCB, [&loader, wait](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
        if (!due) {
            wait(rt, ctx);
            return;
        }
        due = false;
        const std::string name = portablekit::env("SWEEP_STAGE");
        const std::int32_t block = portablekit::kernel().allocate_block("stage sweep", 1u, 0x100u, 0x10u);
        const std::uint32_t text = portablekit::kernel().find_block(block)->address;
        for (std::size_t i = 0; i <= name.size(); ++i) rt.memory().store8(text + i, i < name.size() ? name[i] : 0);
        std::cout << "[sweep] " << name << std::endl;
        portablekit::set_skip_module_first_run(true);
        portablekit::kernel().call_guest(ctx, loader.create, {text},
                                         [&loader](psprecomp::AllegrexContext &ctx, std::uint32_t object) {
                                             if (object == 0u) {
                                                 std::cout << "[sweep] no data.cnf" << std::endl;
                                                 portablekit::kernel().runtime().stop("stage sweep failed");
                                                 portablekit::kernel().finish(ctx, 0u);
                                                 return;
                                             }
                                             detail::sweep_step(loader, ctx, object, 0u);
                                         });
    });
}

} // namespace mgs
