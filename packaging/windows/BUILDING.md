# Windows release preparation

These commands are for the maintainer. Players receive one ZIP per edition
and supply an ISO through the existing first-run setup. Nothing below has
been built or launch-tested on Windows yet.

Use a Windows x64 machine with Python 3.11+, CMake, Ninja, Git, the Vulkan SDK
(including glslangValidator), llvm-mingw **20260922 UCRT x86_64**, and the
**SDL3 3.4.16 MinGW development package**. Use the same llvm-mingw toolchain
for the executable and every overlay DLL. The framework downloads its pinned
FFmpeg 7.1.5 LGPL Windows DLLs during native Windows configuration. Unix-to-MinGW
x64 builds instead compile the minimal ATRAC3/ATRAC3plus/H.264 libraries from
source. Set `PORTABLEKIT_FFMPEG_WINDOWS_PREBUILT=ON` to use the full prebuilt
fallback there. The packager follows imports: swresample is included only when
another DLL needs it.

Copy this checkout including its local PortableKit changes and the generated
`profiles/{po,pop}/{generated,overlays,units}` directories. Git alone does not
carry those ignored corpora. Do not copy Linux build directories into the
Windows build directories. No game images or saves are needed to compile.

In PowerShell at the repository root, adjust these paths to your installations:

```powershell
$toolchain = 'C:\tools\llvm-mingw-20260922-ucrt-x86_64'
$sdl = 'C:\tools\SDL3-3.4.16'
$env:PATH = "$toolchain\bin;$env:VULKAN_SDK\Bin;$env:PATH"
foreach ($edition in @('po', 'pop')) {
    cmake -S . -B "out/windows/$edition" -G Ninja `
        "-DMGS_EDITION=$edition" -DCMAKE_BUILD_TYPE=Release `
        -DPORTABLEKIT_RELEASE=ON -DPORTABLEKIT_RENDERER=ON `
        -DPORTABLEKIT_FFMPEG=bundled -DPSPRECOMP_GENERATED_JOBS=2 `
        "-DCMAKE_C_COMPILER=$toolchain/bin/clang.exe" `
        "-DCMAKE_CXX_COMPILER=$toolchain/bin/clang++.exe" `
        "-DCMAKE_PREFIX_PATH=$sdl/x86_64-w64-mingw32"
    if ($LASTEXITCODE -ne 0) { throw "Configuration failed: $edition" }
    cmake --build "out/windows/$edition" --parallel 2
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $edition" }
}
```

Check configuration reports the Vulkan renderer and generated AOT units for
both targets. The packager requires every overlay named by the local profile,
so a build with missing modules cannot silently become a release.

Obtain `NotoSansCJKjp-Regular.otf` and its license from the URLs pinned in
`portablekit/packaging/sources.sh`, checking their SHA-256 values there. An
existing verified Linux release dependency download can be reused. Put them
in `out/windows-resources/` as `NotoSansCJKjp-Regular.otf` and
`NotoSansCJK-LICENSE.txt`. Release builds do not automatically fetch the font.

Package only after both builds finish:

```powershell
python scripts/package-windows.py both `
    --toolchain "$toolchain" --sdl-license "$sdl/LICENSE.txt" `
    --font out/windows-resources/NotoSansCJKjp-Regular.otf `
    --font-license out/windows-resources/NotoSansCJK-LICENSE.txt `
    --version 0.1.0-local
```

Use `po` or `pop` to package one edition. The script never builds anything.
It copies only the game executable, expected overlay DLLs, transitive runtime
DLLs, font and notices. It checks x64 PE format and missing dependencies,
rejects unexpected DLL prerequisites even when installed on the build machine,
records source/corpus and staged-file hashes in `RELEASE-MANIFEST.json`,
starts the staged executable with `--version` and a restricted PATH, checks
ZIP integrity, then writes ZIPs and SHA-256 sidecars under
`out/release-windows-{po,pop}/dist/`. A failed check leaves previous ZIPs intact.
No compiler, headers, `psp_recomp`, source corpus, disc image or save is packed.

The Windows notices cover both minimal source builds (LGPL-2.1-or-later) and
the prebuilt fallback (LGPL-3.0-or-later). `FFmpeg-SOURCE.txt` from the build is
included. Before any public distribution, arrange corresponding source for
that exact FFmpeg build and runtime libraries as their licenses require.
This workflow prepares local packages only.

## Checks before calling a release ready

`python scripts/test-package-windows.py` tests packaging validation without
compiling or executing any Windows binary; it also runs on Linux.

On a clean Windows x64 machine with no development tools, extract each ZIP
and check:

1. Launch with a fresh per-user data directory: setup opens without an ISO.
2. Reject the wrong edition or patched executable with an actionable message.
3. Install the supported ISO, boot the game, load a stage and verify audio.
4. Relaunch: game starts directly. Move the extracted program folder and retry.
5. Run with `--install`: setup reopens. Existing saves remain intact.

Use temporary `MGSPO_DATA_DIR` and `MGSPOP_DATA_DIR` values for testing so real
installations stay untouched. The packager's `--version` check verifies basic
startup only; it does not replace these graphics, installer and gameplay checks.
