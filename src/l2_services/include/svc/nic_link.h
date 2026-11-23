// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace mister::svc {

enum class NicLink : std::uint8_t { Off, Eth0, Eth1, Macvlan, Tap, kCount };

[[nodiscard]] constexpr NicLink nic_link_of(std::byte stored) noexcept {
    const auto n = std::to_integer<std::size_t>(stored);
    return n < std::to_underlying(NicLink::kCount) ? static_cast<NicLink>(n) : NicLink::Off;
}

[[nodiscard]] constexpr std::string_view nic_link_name(NicLink link) noexcept {
    switch (link) {
        case NicLink::Off:
            return "Off";
        case NicLink::Eth0:
            return "eth0";
        case NicLink::Eth1:
            return "eth1";
        case NicLink::Macvlan:
            return "macvlan";
        case NicLink::Tap:
            return "tap0";
        case NicLink::kCount:
            break;
    }
    return "Off";
}

}
