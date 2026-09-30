// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/net_probe.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>

namespace mister::os {
namespace {

bool is_wlan(std::string_view name) noexcept {
    return name.size() >= 4 && name.substr(0, 4) == "wlan";
}

}  // namespace

NetPresence net_presence_from_list(std::span<const Ipv4If> ifs) noexcept {
    NetPresence out{};
    for (const auto& f : ifs) {

        if (f.octet0 == 169 && f.octet1 == 254) continue;
        if (f.name == "eth0") out.wired = true;
        if (is_wlan(f.name)) out.wifi = true;
    }
    return out;
}

[[nodiscard]] Ex<NetPresence> net_presence() {
    ifaddrs* ifaddr = nullptr;
    if (::getifaddrs(&ifaddr) == -1) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }

    NetPresence out{};
    for (const ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_name == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        const auto* sin = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
        std::uint8_t octets[4];
        std::memcpy(octets, &sin->sin_addr.s_addr, sizeof octets);
        const Ipv4If one{std::string_view{ifa->ifa_name}, octets[0], octets[1]};
        const NetPresence p = net_presence_from_list(std::span<const Ipv4If>(&one, 1));
        out.wired = out.wired || p.wired;
        out.wifi = out.wifi || p.wifi;
    }
    ::freeifaddrs(ifaddr);
    return out;
}

}  // namespace mister::os
