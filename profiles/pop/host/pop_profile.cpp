// Everything PortableKit has to be told about Metal Gear Solid: Portable Ops+.
// One source for every release; the build picks one with MGS_REGION (US, EU or
// JP). The framework reads the rest out of the executable. Values were read
// off each disc: the id and title from PSP_GAME/PARAM.SFO, the hashes from the
// files, the tag from offset 0xD0 of EBOOT.BIN, the image size from the ELF's
// program headers. The engine addresses of the other releases were found by
// matching the US code they come from, and for the data the engine shares
// with stages, by linking the same stage for each release (stage-images.py).

#include "boot_skip.hpp"
#include "camera.hpp"
#include "detail_distance.hpp"
#include "interpolation.hpp"
#include "presence.hpp"
#include "profile.hpp"
#include "savempo.hpp"
#include "umd_key_table.hpp"
#include "widescreen.hpp"

#include "psprecomp/elf32.hpp"

namespace portablekit {
namespace {

constexpr std::uint32_t kBase = 0x08804000u;

struct Release {
    const char *disc_id;
    const char *disc_id_display;
    const char *release_name;
    // EBOOT.BIN, and what it decrypts to (identical to BOOT.BIN on the disc).
    const char *encrypted_executable_sha256;
    const char *executable_sha256;
    // The engine's routine appending a 32-bit value to a network message, and
    // the return address of the lobby join request's call to it (savempo.hpp).
    std::uint32_t append_u32;
    std::uint32_t join_return;
    // The actors' action bitsets, the engine's pad state and its globals.
    std::uint32_t action_flags;
    std::uint32_t set_action;  // the action flag setter (camera.hpp)
    std::uint32_t pad;
    std::uint32_t globals;
    // The view builder and the x scale it loads (widescreen.hpp).
    std::uint32_t build_view;
    std::uint32_t x_scale;
    std::uint32_t stage_request;  // presence.hpp
    std::uint32_t system_language;
    // The three save slots' SAVEDATA_DIRECTORY: the disc id and the slot.
    const char *save_slots[3];
    // The LOD command's handler and its record table (detail_distance.hpp).
    std::uint32_t register_lod;
    std::uint32_t lod_table;
    // The procedure call, the title procedure's number, and in title.prx the
    // logo actor's update and its first and last states (boot_skip.hpp).
    std::uint32_t call_procedure;
    std::uint32_t title_procedure;
    std::uint32_t logo_update, logo_first, logo_done;
};

#if defined(MGS_REGION_EU)
// Five languages on the disc; English until the console's language is a setting.
constexpr Release kRelease{"ULES01003", "ULES-01003", "the European release (version 1.00)",
                           "c4790bf9e3420eb885a64fcedf2ef682b891d70c434092ecb5f1c62a4c86c61b",
                           "8291454649b98b5b51fcd28352dbba42b9ce8722d21a01e84e9a2464cce7ecd3",
                           0x1446B0u, 0x13966Cu, 0x299A2Cu, 0xB9B84u, 0x1E6930u, 0x17A6D0u, 0x49968u, 0x170F6Cu, 0x41FDCu, 1u,
                           {"ULES01003001", "ULES01003002", "ULES01003003"},
                           0x15A68u, 0x19E318u, 0x46738u, 104u, 0x33190u, 0x330B0u, 0x32F14u};
#elif defined(MGS_REGION_JP)
// Japanese console, Japanese.
constexpr Release kRelease{"ULJM05261", "ULJM-05261", "the Japanese release (version 1.03)",
                           "daa34f17a345329d4a14602d58c72c9a70108c2b9ce335451dd2e97260d91bfb",
                           "1f8f93490d7f5cc57f2e8eec6e70c1f5b6474dd367ddbfd31ce87ef78228a2dc",
                           0x1424DCu, 0x137498u, 0x295E0Cu, 0xB8544u, 0x1E2D10u, 0x1732B0u, 0x48DB4u, 0x16A82Cu, 0x41430u, 0u,
                           {"ULJM05261001", "ULJM05261002", "ULJM05261003"},
                           0x157C0u, 0x196D18u, 0x45B84u, 103u, 0x33048u, 0x32F68u, 0x32DCCu};
#else
// North American console, English.
constexpr Release kRelease{"ULUS10290", "ULUS-10290", "the North American release (version 1.01)",
                           "b939e63771a74ff47465751cfef8ed4f57c35ddd3009cc99d6607064e789ed91",
                           "61eaaaa771e5dabcca07b6df62137e39709caa62030f5cc8ddc809e333a6943b",
                           0x142598u, 0x137554u, 0x294D5Cu, 0xB8644u, 0x1E1C60u, 0x1731F0u, 0x48E1Cu, 0x16A754u, 0x41498u, 1u,
                           {"ULUS10290001", "ULUS10290002", "ULUS10290003"},
                           0x157C0u, 0x196C18u, 0x45BECu, 103u, 0x330A0u, 0x32FC0u, 0x32E24u};
#endif

constexpr SaveFolder kSaveFolders[] = {
    {kRelease.save_slots[0], "Save 1", true},
    {kRelease.save_slots[1], "Save 2", true},
    {kRelease.save_slots[2], "Save 3", true},
};

constexpr mgs::SaveMpoMark kSaveMpoMark{kBase + kRelease.append_u32, kBase + kRelease.join_return};

// The player's input step lives in each stage module: the shape of its first
// twelve instructions, the same in every release.
constexpr std::uint32_t kPlayerInputShape[] = {0x27bd0000u, 0xafb40000u, 0x24060000u, 0x24070000u,
                                               0xafb10000u, 0x00a08821u, 0x24050000u, 0xafb00000u,
                                               0x00808021u, 0xafbf0000u, 0xafb60000u, 0xafb50000u};

void module_code_ready(psprecomp::Runtime &runtime, std::string_view name, std::uint32_t base,
                       std::span<const std::uint8_t> code) {
    if (name != "mgp_stage") return;
    mgs::boot_skip::module_ready(runtime, name, base, code);
    mgs::install_camera_in_module(runtime, base, code, kPlayerInputShape, kBase + kRelease.action_flags,
                                  kBase + kRelease.pad, kBase + kRelease.globals,
                                  kBase + kRelease.set_action);
    mgs::install_camera_follow_in_module(runtime, base, code);
}

void patch_image(psprecomp::Runtime &runtime, const psprecomp::Elf32Image &) {
    mgs::install_savempo_mark(runtime, kSaveMpoMark);
    mgs::install_widescreen(runtime, {kBase + kRelease.build_view, kBase + kRelease.x_scale});
    mgs::presence::install(runtime, kBase + kRelease.stage_request);
    // No foot shadow fade: PO's shadow update is not in this engine.
    mgs::detail_distance::install(runtime, kBase + kRelease.register_lod, kBase + kRelease.lod_table, 0u);
    // The bank pointer is the word after the globals' two; the return word is var 0x3E4.
    mgs::boot_skip::install(runtime, {kBase + kRelease.call_procedure, kBase + kRelease.globals + 8u, true, 0x3E4u,
                                      kRelease.title_procedure, kRelease.logo_update, kRelease.logo_first,
                                      kRelease.logo_done});
}

} // namespace

const GameProfile &game() {
    static const GameProfile profile{
        .app_name = "PortableOpsPlusNative",
        .project_name = "Portable Ops+ Native",
        .env_prefix = "MGSPOP",
        .data_organization = "PortableOpsPlusNative",
        .data_application = kRelease.disc_id,

        .disc_id = kRelease.disc_id,
        .disc_id_display = kRelease.disc_id_display,
        .game_title = "METAL GEAR SOLID PORTABLE OPS +",
        .executable_path_on_disc = "PSP_GAME/SYSDIR/EBOOT.BIN",
        .param_sfo_path_on_disc = "PSP_GAME/PARAM.SFO",
        .release_name = kRelease.release_name,
        .encrypted_executable_sha256 = kRelease.encrypted_executable_sha256,
        .executable_sha256 = kRelease.executable_sha256,
        .decryption_tag = mgs::kUmdTag,
        .decryption_key = {},
        .decryption_key_table = mgs::kUmdKeyTable,

        // Relocatable; the image with BSS ends at 0x29A180 at most (EU), well
        // inside the 32 MiB of a PSP-1000 at the usual user base.
        .load_base = psprecomp::kDefaultPspUserLoadBase,
        .guest_ram_bytes = 32u * 1024u * 1024u,
        .boot_path = "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN",
        // Stage code arrives as modules (PSP_GAME/USRDIR/stage), not overlay slots.
        .overlay_slots = {},

        .save_game_name = kRelease.disc_id,
        .save_folders = kSaveFolders,

        .adhoc_product_code = kRelease.disc_id,
        .infrastructure_host = &mgs::savempo_host,
        .infrastructure_url = &mgs::savempo_url,

        .patch_loaded_image = &patch_image,
        .module_code_ready = &module_code_ready,
        .draw_game_settings = &mgs::boot_skip::draw,
        .reset_game_settings = &mgs::boot_skip::reset,
        .camera = &mgs::kCameraDriver,
        .view_aspect_frame = &mgs::view_aspect_frame,
        .adjustable_fov = true,
        .adjustable_detail_distance = true,
        .interpolation_thresholds = &mgs::kInterpolationThresholds,

        .system_language = kRelease.system_language,
        // Every release confirms with circle whatever the console says, so
        // report circle and keep the menu, the system dialogs and the default
        // face button in step with it.
        .confirm_button = 0u,
    };
    return profile;
}

} // namespace portablekit
