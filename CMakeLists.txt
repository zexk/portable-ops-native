cmake_minimum_required(VERSION 3.20)
project(PortableOpsNative VERSION 0.1.0 LANGUAGES CXX)

# PortableKit brings the recompiler, the runtime and the system layer, and
# defines portablekit_add_game(). It builds one game per configured tree, so
# each edition gets its own build directory:
#   cmake -S . -B out/po  -G Ninja -DMGS_EDITION=po
#   cmake -S . -B out/pop -G Ninja -DMGS_EDITION=pop
set(MGS_EDITION "po" CACHE STRING "Edition to build: po (Portable Ops) or pop (Portable Ops+)")
set_property(CACHE MGS_EDITION PROPERTY STRINGS po pop)

add_subdirectory(portablekit)

if(MGS_EDITION STREQUAL "po")
    set(target PortableOpsNative)
elseif(MGS_EDITION STREQUAL "pop")
    set(target PortableOpsPlusNative)
else()
    message(FATAL_ERROR "MGS_EDITION must be po or pop, not '${MGS_EDITION}'")
endif()

# Each profile directory holds its own generated/ corpus; common/ is shared MGS code.
portablekit_add_game(${target}
    PROFILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/profiles/${MGS_EDITION}"
    SOURCES host/${MGS_EDITION}_profile.cpp)
target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/common/host")
