# Third-party notices

PortableKit: MIT License, `PortableKit-LICENSE.txt`. The project-specific code has no separate license declared in this checkout. These local packages include precompiled game code; players supply their own disc image.

## Compiled into the program

### Dear ImGui 1.92.9b

The library window, the in-game menu and the setup screens. Copyright (c) 2014-2026 Omar Cornut. MIT License: `DearImGui-LICENSE.txt`. <https://github.com/ocornut/imgui>

### tiny-AES-c

AES for preparing the executable from the disc image and for save data. Commit `23856752fbd139da0b8ca6e471a13d5bcc99a08d`, released into the public domain under the Unlicense: `tiny-AES-c-UNLICENSE.txt`. <https://github.com/kokke/tiny-AES-c>

### stb_truetype 1.26

Draws the game's text. Copyright (c) 2017 Sean Barrett. Used under the public-domain dedication it offers as an alternative to the MIT License:

> This is free and unencumbered software released into the public domain. Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either in source code form or as a compiled binary, for any purpose, commercial or non-commercial, and by any means.

<https://github.com/nothings/stb>

### stb_image 2.30 and stb_image_write 1.16

Read and write the PNG images of HD texture packs. Copyright (c) 2017 Sean Barrett. Used under the same public-domain dedication as stb_truetype above. <https://github.com/nothings/stb>

### xxHash 0.8.3

Hashes textures for HD texture packs. Copyright (c) 2012-2021 Yann Collet. BSD 2-Clause License: `xxHash-LICENSE.txt`. <https://github.com/Cyan4973/xxHash>

## Shipped runtime libraries

### SDL3 3.4.16

SDL3.dll, from the MinGW development package. zlib License:
`SDL3-LICENSE.txt`. Source: <https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16>.

### FFmpeg 7.1.5, prebuilt (Windows)

The Windows build uses the unmodified prebuilt LGPL shared build of FFmpeg 7.1.5 (commit `7d0e842004`) from BtbN/FFmpeg-Builds, built from FFmpeg's LGPL parts only and with `--enable-version3`, so it is licensed under the GNU Lesser General Public License, version 3 or later: `FFmpeg-LICENSE.txt`, with the source location in `FFmpeg-SOURCE.txt`. You may replace these libraries with your own builds of a compatible version.

- Libraries: `avcodec-61.dll`, `avutil-59.dll` and `swresample-5.dll`
- Release: <https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-06-30-13-34/ffmpeg-n7.1.5-1-g7d0e842004-win64-lgpl-shared-7.1.zip> (SHA-256 `03a8003e245c08df4277d7b0adc50b93a97ddd4a3aaafea21943c4384df59895`), pinned in `cmake/FFmpeg.cmake`
- Source: <https://github.com/FFmpeg/FFmpeg/tree/7d0e842004>

### llvm-mingw 20260922 runtime

Only runtime DLLs required by the game and its modules are included (such as
libc++.dll, libunwind.dll and libwinpthread-1.dll). No compiler is shipped.
LLVM runtimes: Apache License 2.0 with LLVM Exceptions,
`llvm-mingw-LICENSE.txt`. MinGW-w64 runtime notices: `mingw-w64-COPYING*.txt`.
Sources: <https://github.com/llvm/llvm-project>, <https://www.mingw-w64.org/>.
Toolchain release: <https://github.com/mstorsjo/llvm-mingw/releases/tag/20260922>.

## Font

Noto Sans CJK JP (Noto CJK Sans 2.004), in `fonts/`. Copyright 2014-2021 Adobe,
reserved font name Source. SIL Open Font License 1.1:
`NotoSansCJK-OFL.txt`. <https://github.com/notofonts/noto-cjk>.
