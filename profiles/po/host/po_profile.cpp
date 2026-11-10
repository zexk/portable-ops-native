// Everything PortableKit has to be told about Metal Gear Solid: Portable Ops
// (USA, ULUS10202, disc version 1.02). The framework reads the rest out of the
// executable. Values were read off the disc: the id and title from
// PSP_GAME/PARAM.SFO, the hashes from the files, the tag from offset 0xD0 of
// EBOOT.BIN, the image size from the ELF's program headers.

#include "camera.hpp"
#include "detail_distance.hpp"
#include "fulton.hpp"
#include "fulton_settings.hpp"
#include "interpolation.hpp"
#include "presence.hpp"
#include "profile.hpp"
#include "savempo.hpp"
#include "trace_models.hpp"
#include "umd_key_table.hpp"
#include "widescreen.hpp"

#include "psprecomp/elf32.hpp"

namespace portablekit {
namespace {

// The game's three save slots: SAVEDATA_DIRECTORY is the disc id and the slot.
constexpr SaveFolder kSaveFolders[] = {
    {"ULUS10202001", "Save 1", true},
    {"ULUS10202002", "Save 2", true},
    {"ULUS10202003", "Save 3", true},
};

// The player's input step, the actors' action bitsets, the engine's pad state
// and its pointer to the control settings.
constexpr mgs::CameraAddresses kCamera{0x08804000u + 0xB12F8u, 0x08804000u + 0x2F70BCu, 0x08804000u + 0x2753D8u,
                                       0x08804000u + 0x1F11D0u};

// The camera actor's update, reached through the actor dispatcher.
constexpr std::uint32_t kCameraUpdate = 0x08804000u + 0x10D304u;

// The view builder and the x scale it loads.
constexpr mgs::WidescreenAddresses kWidescreen{0x08804000u + 0x41124u, 0x08804000u + 0x1E5728u};

void patch_image(psprecomp::Runtime &runtime, const psprecomp::Elf32Image &) {
    mgs::install_camera(runtime, kCamera);
    mgs::install_camera_follow(runtime, kCameraUpdate);
    mgs::install_model_trace(runtime, 0x08804000u + 0x45EA4u, 0x08804000u + 0x45954u);
    mgs::install_camera_trace(runtime, kCameraUpdate);
    mgs::install_widescreen(runtime, kWidescreen);
    mgs::fulton::install(runtime);
    mgs::detail_distance::install(runtime, 0x08804000u + 0x15324u, 0x08804000u + 0x20D650u);
    mgs::presence::install(runtime, 0x08804000u + 0x39A20u);
}

} // namespace

const GameProfile &game() {
    static const GameProfile profile{
        .app_name = "PortableOpsNative",
        .project_name = "Portable Ops Native",
        .env_prefix = "MGSPO",
        .data_organization = "PortableOpsNative",
        .data_application = "ULUS10202",

        .disc_id = "ULUS10202",
        .disc_id_display = "ULUS-10202",
        .game_title = "METAL GEAR SOLID PORTABLE OPS",
        .executable_path_on_disc = "PSP_GAME/SYSDIR/EBOOT.BIN",
        .param_sfo_path_on_disc = "PSP_GAME/PARAM.SFO",
        .release_name = "the North American release (version 1.02)",
        // EBOOT.BIN, and what it decrypts to (identical to BOOT.BIN on the disc).
        .encrypted_executable_sha256 = "b64edfa3022f97c184f9d776120a6257bb3041f1f03c00465e1062d41f455957",
        .executable_sha256 = "ca700ea516a98949763410da537a75a08f9723718fab302bcd7ef1e6185cadf8",
        .decryption_tag = mgs::kUmdTag,
        .decryption_key = {},
        .decryption_key_table = mgs::kUmdKeyTable,

        // Relocatable; the image with BSS ends at 0x2F77C0, well inside the
        // 32 MiB of a PSP-1000 at the usual user base.
        .load_base = psprecomp::kDefaultPspUserLoadBase,
        .guest_ram_bytes = 32u * 1024u * 1024u,
        .boot_path = "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN",
        // Stage code arrives as modules (PSP_GAME/USRDIR/stage), not overlay slots.
        .overlay_slots = {},

        .save_game_name = "ULUS10202",
        .save_folders = kSaveFolders,

        .adhoc_product_code = "ULUS10202",
        .infrastructure_host = &mgs::savempo_host,
        .infrastructure_url = &mgs::savempo_url,

        .register_extra_hle = &mgs::fulton_assets::install,
        .patch_loaded_image = &patch_image,
        .module_code_ready = &mgs::fulton::module_ready,
        .draw_game_settings = &mgs::fulton_settings::draw,
        .reset_game_settings = &mgs::fulton_settings::reset,
        .game_settings_screen = &mgs::fulton_settings::screen,
        .camera = &mgs::kCameraDriver,
        .view_aspect_frame = &mgs::view_aspect_frame,
        .adjustable_fov = true,
        .adjustable_detail_distance = true,
        .interpolation_thresholds = &mgs::kInterpolationThresholds,

        // North American console, English. The game confirms with circle
        // whatever the console says, so report circle and keep the menu,
        // the system dialogs and the default face button in step with it.
        .system_language = 1u,
        .confirm_button = 0u,
    };
    return profile;
}

} // namespace portablekit
