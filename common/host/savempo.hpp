#pragma once

// Where Portable Ops' online play goes now: SaveMGO's community server
// (savempo.com) stands in for Konami's, which closed in 2012. These are the
// redirections the SaveMPO plugin makes on a PSP, read from its code:
// - the lobby gates (mpogate01/02/03) and the account and terms-of-use sites
//   go to mpo.savemgo.com, the STUN server (mpostun*) to stun.mgo2pc.com;
// - HTTPS is spoken as plain HTTP;
// - a path naming the policy (terms of use) gets "_savempo" appended, the
//   server's own version of the page.
// <PREFIX>_ONLINE_SERVER=host[:port] sends all of it to another server instead,
// such as our own (server/), whose web pages are then on that port (80 when
// absent).

#include "profile.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace mgs {

inline constexpr std::string_view kSaveMpoServer = "mpo.savemgo.com";
inline constexpr std::string_view kSaveMpoStun = "stun.mgo2pc.com";

// The server chosen with <PREFIX>_ONLINE_SERVER, as host[:port], or empty.
inline std::string online_server() {
    const char *server = portablekit::env("ONLINE_SERVER");
    return server != nullptr ? server : "";
}

inline std::string savempo_host(std::string_view host) {
    const std::string server = online_server();
    const bool own = !server.empty();
    if (host.starts_with("mpogate") || host == "mpoweb.konamionline.com" || host == "info.service.konamionline.com")
        return own ? server.substr(0, server.find(':')) : std::string(kSaveMpoServer);
    if (host.starts_with("mpostun")) return own ? server.substr(0, server.find(':')) : std::string(kSaveMpoStun);
    return std::string(host);
}

inline std::string savempo_url(std::string_view url) {
    for (const std::string_view scheme : {"https://", "http://"})
        if (url.starts_with(scheme)) {
            url.remove_prefix(scheme.size());
            break;
        }
    const std::size_t slash = url.find('/');
    const std::string_view host = url.substr(0, slash);
    std::string path(slash == std::string_view::npos ? std::string_view{} : url.substr(slash));
    std::string target = savempo_host(host);
    if (target == kSaveMpoServer && path.find("policy") != std::string::npos) path += "_savempo";
    if (const std::string server = online_server(); !server.empty() && target == server.substr(0, server.find(':')))
        target = server;
    return "http://" + target + path;
}


// The lobby lets a client in only when the user id in its join request
// (message 0x3003) carries 0x10000000, the mark of a patched game; the plugin
// adds it at the one call that appends that id. The routine appending a
// 32-bit value to a message is replaced here by one that does the same,
// big-endian at 0x24 + length with the length at 0x438, and adds the mark when
// called from that site.
struct SaveMpoMark {
    std::uint32_t append_u32 = 0u;   // the routine, in the loaded executable
    std::uint32_t join_return = 0u;  // the return address of the join request's call to it
};
inline SaveMpoMark savempo_mark;

inline void savempo_append_u32(psprecomp::CorpusRuntime &rt, psprecomp::AllegrexContext &ctx) {
    auto &memory = static_cast<psprecomp::Runtime &>(rt).memory();
    const std::uint32_t message = ctx.gpr[4];
    const std::uint32_t length = memory.load32(message + 0x438u);
    std::uint32_t result = static_cast<std::uint32_t>(-0x1F5);  // full
    if (static_cast<std::int32_t>(length) < 0x3FD) {
        std::uint32_t value = memory.load32(ctx.gpr[5]);
        if (ctx.gpr[31] == savempo_mark.join_return) value |= 0x10000000u;
        for (std::uint32_t i = 0; i < 4u; ++i)
            memory.store8(message + 0x24u + length + i, static_cast<std::uint8_t>(value >> (24u - 8u * i)));
        memory.store32(message + 0x438u, length + 4u);
        result = 0u;
    }
    ctx.gpr[2] = result;
    ctx.pc = ctx.gpr[31];
}

inline void install_savempo_mark(psprecomp::Runtime &runtime, SaveMpoMark mark) {
    savempo_mark = mark;
    runtime.register_function(mark.append_u32, &savempo_append_u32, "savempo_append_u32");
}

} // namespace mgs
