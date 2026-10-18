// Everything PortableKit has to be told about Metal Gear Solid: Portable Ops+
// (USA, ULUS10290, disc version 1.01). The framework reads the rest out of the
// executable. Values were read off the disc: the id and title from
// PSP_GAME/PARAM.SFO, the hashes from the files, the tag from offset 0xD0 of
// EBOOT.BIN, the image size from the ELF's program headers.

#include "profile.hpp"
#include "stage_sweep.hpp"
#include "umd_key_table.hpp"

#include "psprecomp/elf32.hpp"

namespace portablekit {
namespace {

constexpr SaveFolder kSaveFolders[] = {
    {"ULUS10290", "Game data", true},
};

// The engine's stage loader (create, step) and the game's sceKernelDelayThread import
// stub (the engine always loads at 0x08804000).
constexpr mgs::StageLoader kStageLoader{0x08804000u + 0x41498u, 0x08804000u + 0x4115Cu, 0x08804000u + 0x159874u};

void register_hle(HleRegistrar &hle) { mgs::register_stage_sweep(hle, kStageLoader); }

} // namespace

const GameProfile &game() {
    static const GameProfile profile{
        .app_name = "PortableOpsPlusNative",
        .project_name = "Portable Ops+ Native",
        .env_prefix = "MGSPOP",
        .data_organization = "PortableOpsPlusNative",
        .data_application = "ULUS10290",

        .disc_id = "ULUS10290",
        .disc_id_display = "ULUS-10290",
        .game_title = "METAL GEAR SOLID PORTABLE OPS +",
        .executable_path_on_disc = "PSP_GAME/SYSDIR/EBOOT.BIN",
        .param_sfo_path_on_disc = "PSP_GAME/PARAM.SFO",
        .release_name = "the North American release (version 1.01)",
        // EBOOT.BIN, and what it decrypts to (identical to BOOT.BIN on the disc).
        .encrypted_executable_sha256 = "b939e63771a74ff47465751cfef8ed4f57c35ddd3009cc99d6607064e789ed91",
        .executable_sha256 = "61eaaaa771e5dabcca07b6df62137e39709caa62030f5cc8ddc809e333a6943b",
        .decryption_tag = mgs::kUmdTag,
        .decryption_key = {},
        .decryption_key_table = mgs::kUmdKeyTable,

        // Relocatable; the image with BSS ends at 0x295480, well inside the
        // 32 MiB of a PSP-1000 at the usual user base.
        .load_base = psprecomp::kDefaultPspUserLoadBase,
        .guest_ram_bytes = 32u * 1024u * 1024u,
        .boot_path = "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN",
        // Stage code arrives as modules (PSP_GAME/USRDIR/stage), not overlay slots.
        .overlay_slots = {},

        .save_game_name = "ULUS10290",
        .save_folders = kSaveFolders,

        .adhoc_product_code = "ULUS10290",

        .register_extra_hle = &register_hle,

        // North American console: English, cross confirms.
        .system_language = 1u,
        .confirm_button = 1u,
    };
    return profile;
}

} // namespace portablekit
