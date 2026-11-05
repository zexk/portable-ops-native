# Cross-compiles the Windows x64 build from Linux with llvm-mingw.
#
#   cmake -S . -B out/windows/pop -G Ninja --toolchain packaging/windows/llvm-mingw.cmake \
#       -DLLVM_MINGW=<llvm-mingw> -DSDL3_MINGW=<SDL3-devel>/x86_64-w64-mingw32 \
#       -DVULKAN_MINGW=<dir with include/vulkan and lib/libvulkan-1.a> -DMGS_EDITION=pop ...
#
# LLVM_MINGW is llvm-mingw's Linux (UCRT) release; on NixOS its binaries need
# nix-ld. SDL3_MINGW is from SDL3's MinGW development package. VULKAN_MINGW
# holds the Vulkan headers and an import library made from the loader's export
# list: llvm-dlltool -m i386:x86-64 -d vulkan-1.def -l lib/libvulkan-1.a.
# Each may also come from the environment.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

foreach(variable LLVM_MINGW SDL3_MINGW VULKAN_MINGW)
    if(NOT ${variable} AND DEFINED ENV{${variable}})
        set(${variable} "$ENV{${variable}}")
    endif()
    if(NOT ${variable})
        message(FATAL_ERROR "Set ${variable} (see packaging/windows/llvm-mingw.cmake)")
    endif()
    # Try-compile projects see the same paths.
    list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ${variable})
endforeach()

set(CMAKE_C_COMPILER "${LLVM_MINGW}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW}/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER "${LLVM_MINGW}/bin/x86_64-w64-mingw32-windres")
set(CMAKE_AR "${LLVM_MINGW}/bin/llvm-ar")
set(CMAKE_RANLIB "${LLVM_MINGW}/bin/llvm-ranlib")

set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW}/x86_64-w64-mingw32" "${SDL3_MINGW}" "${VULKAN_MINGW}")
# Build tools (glslang, Python) come from the host; libraries and headers
# only from the Windows roots.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(Vulkan_INCLUDE_DIR "${VULKAN_MINGW}/include" CACHE PATH "")
set(Vulkan_LIBRARY "${VULKAN_MINGW}/lib/libvulkan-1.a" CACHE FILEPATH "")
set(SDL3_DIR "${SDL3_MINGW}/lib/cmake/SDL3" CACHE PATH "")
