#pragma once

// Where Portable Ops' online play goes now: SaveMGO's community server
// (savempo.com) stands in for Konami's, which closed in 2012. These are the
// redirections the SaveMPO plugin makes on a PSP, read from its code:
// - the lobby gates (mpogate01/02/03) and the account and terms-of-use sites
//   go to mpo.savemgo.com, the STUN server (mpostun*) to stun.mgo2pc.com;
// - HTTPS is spoken as plain HTTP;
// - a path naming the policy (terms of use) gets "_savempo" appended, the
//   server's own version of the page.

#include <string>
#include <string_view>

namespace mgs {

inline constexpr std::string_view kSaveMpoServer = "mpo.savemgo.com";
inline constexpr std::string_view kSaveMpoStun = "stun.mgo2pc.com";

inline std::string savempo_host(std::string_view host) {
    if (host.starts_with("mpogate") || host == "mpoweb.konamionline.com" || host == "info.service.konamionline.com")
        return std::string(kSaveMpoServer);
    if (host.starts_with("mpostun")) return std::string(kSaveMpoStun);
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
    const std::string target = savempo_host(host);
    if (target == kSaveMpoServer && path.find("policy") != std::string::npos) path += "_savempo";
    return "http://" + target + path;
}

} // namespace mgs
