cmake_minimum_required(VERSION 3.20)
project(PortableOpsNative VERSION 0.1.0 LANGUAGES C CXX)

# PortableKit brings the recompiler, the runtime and the system layer, and
# defines portablekit_add_game(). It builds one game per configured tree, so
# each edition gets its own build directory:
#   cmake -S . -B out/po  -G Ninja -DMGS_EDITION=po
#   cmake -S . -B out/pop -G Ninja -DMGS_EDITION=pop
# pop-eu and pop-jp are Portable Ops+'s European and Japanese releases.
set(MGS_EDITION "po" CACHE STRING "Edition to build: po (Portable Ops), pop, pop-eu or pop-jp (Portable Ops+)")
set_property(CACHE MGS_EDITION PROPERTY STRINGS po pop pop-eu pop-jp)

add_subdirectory(portablekit)
# PortableKit's own setting stays in its directory; the packagers read the
# game's compile commands too.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MGS_EDITION STREQUAL "po")
    set(target PortableOpsNative)
    set(profile po)
elseif(MGS_EDITION MATCHES "^pop(-eu|-jp)?$")
    set(target PortableOpsPlusNative)
    set(profile pop)
else()
    message(FATAL_ERROR "MGS_EDITION must be po, pop, pop-eu or pop-jp, not '${MGS_EDITION}'")
endif()

# Each edition's directory holds its own generated/ corpus; the releases of one
# game share their profile source; common/ is shared MGS code.
portablekit_add_game(${target}
    PROFILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/profiles/${MGS_EDITION}"
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/profiles/${profile}/host/${profile}_profile.cpp")
target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/common/host")
if(MGS_EDITION MATCHES "-(eu|jp)$")
    string(TOUPPER "${CMAKE_MATCH_1}" region)
    target_compile_definitions(${target} PRIVATE MGS_REGION_${region})
endif()

# The online server (server/): one C file with no dependencies, shipped beside
# Portable Ops+.
add_executable(mpo-server server/mpo_server.c)
set_target_properties(mpo-server PROPERTIES C_STANDARD 99 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(WIN32)
    target_link_libraries(mpo-server PRIVATE ws2_32)
endif()
