#pragma once

// Game > Controls > Quick equip: the D-pad picks one of the four inventory
// slots without the item wheel. Pressing the wheel's drop button while a
// slot's direction is held drops its item; pressing its send button sends
// the item to base, for a soldier who can deliver (ability 0x10000, as the
// wheel's send mode asks). The buttons are the wheel's own, read from the
// control settings each frame (+0x84/+0x90/+0x88/+0x8C slots 0-3, +0x68 drop,
// +0xA4 send: the D-pad, L and R by default), so a changed key configuration
// carries over.
//
// After the engine's pad update the D-pad bits leave the GV pad words (held,
// pressed, released), so nothing else reads them, the game's D-pad camera
// included, but only while the wheel is idle (actor +0x54 is 0) and its own
// check says it may be used: open, the wheel reads them itself. L and R are
// the game's too: a press during a D-pad hold leaves the words until that
// button is released (the game then sees neither the press nor the release),
// and one already held when the D-pad goes down is left alone. A new press
// runs the calls the wheel's commit makes.

#include "guest_wrap.hpp"
#include "install/user_data.hpp"
#include "portablekit/hle_extension.hpp"
#include "psprecomp/runtime.hpp"
#include "settings/settings.hpp"

#if defined(PORTABLEKIT_HAS_RENDERER)
#include "ui/widgets.hpp"
#endif

#include <cstdint>
#include <exception>
#include <iostream>

namespace mgs::quick_equip {

struct Addresses {
    std::uint32_t pad_update;  // reads the pad driver, then derives the GV pad words
    std::uint32_t pad;         // GV pad 0: held, pressed at +0x14, released at +0x18
    std::uint32_t wheel;       // pointer to the item wheel actor
    std::uint32_t settings;    // pointer to the control settings
    std::uint32_t player;      // pointer to the controlled player's actor
    std::uint32_t blocked;     // (wheel) -> nonzero while the wheel may not be used
    std::uint32_t choose;      // (wheel, slot, other slot) -> above 0 when done
    std::uint32_t refresh;     // (wheel, -1)
    std::uint32_t show;        // (wheel, slot)
    std::uint32_t drop;        // (wheel): the slot at wheel +0x4C
    std::uint32_t send;        // (wheel): the slot at wheel +0x4C
    std::uint32_t ability;     // (player, bit) -> nonzero when the soldier has it
    std::uint32_t sound;       // (id): the wheel's refusal is 0x153
};

constexpr std::uint32_t kSlotMasks[4] = {0x84u, 0x90u, 0x88u, 0x8Cu};
constexpr std::uint32_t kDropMask = 0x68u, kSendMask = 0xA4u;
constexpr std::uint32_t kDelivery = 0x10000u, kRefused = 0x153u, kNone = 0xFFFFFFFFu;

inline Addresses at{};
inline GuestWrap pad_update;
inline bool enabled = false;
inline bool loaded = false;
inline std::uint32_t last_dpad = 0u;
inline std::uint32_t last_buttons = 0u;  // the drop and send buttons held last frame
inline std::uint32_t swallowed = 0u;     // of those, pressed during a D-pad hold and kept from the game

inline void load() {
    if (loaded) return;
    loaded = true;
    try {
        const auto entries = portablekit::install::read_settings_file(portablekit::install::user_data_directory());
        if (const auto found = entries.find("input.quick_equip"); found != entries.end()) enabled = found->second == "1";
    } catch (const std::exception &) {
    }
}

inline void save() {
    try {
        const auto root = portablekit::install::user_data_directory();
        auto entries = portablekit::install::read_settings_file(root);
        entries["input.quick_equip"] = enabled ? "1" : "0";
        portablekit::install::write_settings_file(root, entries);
    } catch (const std::exception &error) {
        std::cerr << "[quick-equip] cannot save setting: " << error.what() << std::endl;
    }
}

inline bool dpad_is_stick() {
    return portablekit::settings::current().right_stick == portablekit::settings::RightStick::DPad;
}

using portablekit::hle_extension::call_guest;

// The wheel's commit (0x9342C in PO): the cursor on the slot, the choice,
// then the redraw; choosing the chosen slot again puts the item away.
inline void choose(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx, std::uint32_t wheel,
                   std::uint32_t slot) {
    auto &memory = runtime.memory();
    const bool again = memory.load32(wheel + 0x4Cu) == slot;
    memory.store32(wheel + 0x48u, slot);
    call_guest(ctx, at.choose, {wheel, slot, again ? 0u : 1u}, [&runtime, wheel, slot, again](auto &c, std::uint32_t done) {
        if (static_cast<std::int32_t>(done) <= 0) return;
        if (!again) runtime.memory().store32(wheel + 0x4Cu, slot);
        call_guest(c, at.refresh, {wheel, kNone}, [&runtime, wheel, slot, again](auto &c2, std::uint32_t) {
            call_guest(c2, at.show, {wheel, slot}, [&runtime, wheel, again](auto &, std::uint32_t) {
                if (again) runtime.memory().store32(wheel + 0x4Cu, kNone);
            });
        });
    });
}

// The wheel's drop and send modes (0x9414C states 6 and 7): the slot at
// +0x4C, the action, then the redraw.
inline void give_up(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx, std::uint32_t wheel,
                    std::uint32_t slot, std::uint32_t action) {
    runtime.memory().store32(wheel + 0x4Cu, slot);
    call_guest(ctx, action, {wheel}, [wheel, slot](auto &c, std::uint32_t done) {
        if (done == 0u) return;
        call_guest(c, at.refresh, {wheel, kNone}, [wheel, slot](auto &c2, std::uint32_t) {
            call_guest(c2, at.show, {wheel, slot}, [](auto &, std::uint32_t) {});
        });
    });
}

inline void after_pad(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    auto &memory = runtime.memory();
    const std::uint32_t wheel = memory.load32(at.wheel);
    const std::uint32_t settings = memory.load32(at.settings);
    const std::uint32_t held = memory.load32(at.pad);
    if (dpad_is_stick() || !memory.contains(wheel, 0xB0u) || !memory.contains(settings, 0xB0u)) {
        last_dpad = last_buttons = swallowed = 0u;
        return;
    }
    std::uint32_t masks[4], dpad = 0u;
    for (int i = 0; i < 4; ++i) dpad |= masks[i] = memory.load32(settings + kSlotMasks[i]);
    const std::uint32_t drop = memory.load32(settings + kDropMask), send = memory.load32(settings + kSendMask);
    const std::uint32_t now = held & dpad, pressed = now & ~last_dpad;
    const std::uint32_t buttons = held & (drop | send), button_pressed = buttons & ~last_buttons;
    last_dpad = now;
    last_buttons = buttons;
    if (memory.load32(wheel + 0x54u) != 0u) {
        swallowed = 0u;
        return;
    }
    call_guest(ctx, at.blocked, {wheel}, [&runtime, wheel, masks, dpad, now, pressed, buttons, button_pressed, drop,
                                          send](auto &c, std::uint32_t blocked) {
        if (blocked != 0u) {
            swallowed = 0u;
            return;
        }
        auto &m = runtime.memory();
        std::uint32_t held_slot = 0u, pressed_slot = 0u;
        while (held_slot < 4u && (now & masks[held_slot]) == 0u) ++held_slot;
        while (pressed_slot < 4u && (pressed & masks[pressed_slot]) == 0u) ++pressed_slot;
        if (held_slot < 4u) swallowed |= button_pressed;
        swallowed &= buttons;
        for (std::uint32_t word : {0x0u, 0x14u, 0x18u})
            m.store32(at.pad + word, m.load32(at.pad + word) & ~(dpad | swallowed));
        if (held_slot < 4u && (button_pressed & drop) != 0u) {
            give_up(runtime, c, wheel, held_slot, at.drop);
        } else if (held_slot < 4u && (button_pressed & send) != 0u) {
            call_guest(c, at.ability, {m.load32(at.player), kDelivery},
                       [&runtime, wheel, held_slot](auto &c2, std::uint32_t can) {
                           if (can != 0u)
                               give_up(runtime, c2, wheel, held_slot, at.send);
                           else
                               call_guest(c2, at.sound, {kRefused}, [](auto &, std::uint32_t) {});
                       });
        } else if (pressed_slot < 4u) {
            choose(runtime, c, wheel, pressed_slot);
        }
    });
}

inline void on_pad_update(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    if (pad_update.inner) {
        pad_update.inner = false;
        pad_update.original(rt, ctx);
        return;
    }
    load();
    if (!enabled) {
        last_dpad = last_buttons = swallowed = 0u;
        pad_update.original(rt, ctx);
        return;
    }
    auto &runtime = static_cast<psprecomp::Runtime &>(rt);
    pad_update.inner = true;
    call_guest(ctx, pad_update.address, {ctx.gpr[4]},
               [&runtime](auto &c, std::uint32_t) { after_pad(runtime, c); });
}

inline void install(psprecomp::Runtime &runtime, const Addresses &addresses) {
    at = addresses;
    if (pad_update.install(runtime, at.pad_update, &on_pad_update, "mgs_quick_equip_pad"))
        std::cout << "[quick-equip] pad update at 0x" << std::hex << at.pad_update << std::dec << std::endl;
}

inline void reset() {
    enabled = false;
    save();
}

inline void draw() {
#if defined(PORTABLEKIT_HAS_RENDERER)
    using namespace portablekit::ui;
    load();
    const bool stick = dpad_is_stick();
    if (toggle_row("Quick equip", enabled,
                   {stick, stick ? "Right stick is the D-pad" : "",
                    "The D-pad picks an item slot without the item wheel. Holding a direction, press L to drop that "
                    "slot's item or R to send it to base with a soldier who can deliver. The D-pad no longer turns "
                    "the camera."})) {
        enabled = !enabled;
        save();
    }
#endif
}
} // namespace mgs::quick_equip
