// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "svc/nic_link.h"

namespace mister::svc {

struct NicLinkPlan {

    enum class Delivery : std::uint8_t { Direct, Shared, Macvlan, Tap };

    static constexpr std::string_view kOnboard = "eth0";
    static constexpr std::string_view kSecond = "eth1";
    static constexpr std::string_view kMacvlanChild = "ve-amiga";
    static constexpr std::string_view kTapDevice = "tap0";

    NicLink link = NicLink::Off;
    std::string_view iface{};
    std::string_view parent{};
    Delivery delivery = Delivery::Shared;

    [[nodiscard]] static constexpr std::optional<NicLinkPlan> of(NicLink link) noexcept {
        switch (link) {
            case NicLink::Eth0:
                return NicLinkPlan{link, kOnboard, kOnboard, Delivery::Shared};
            case NicLink::Eth1:
                return NicLinkPlan{link, kSecond, kSecond, Delivery::Direct};
            case NicLink::Macvlan:
                return NicLinkPlan{link, kMacvlanChild, kOnboard, Delivery::Macvlan};
            case NicLink::Tap:
                return NicLinkPlan{link, kTapDevice, kTapDevice, Delivery::Tap};
            case NicLink::Off:
            case NicLink::kCount:
                break;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr bool shared() const noexcept { return delivery == Delivery::Shared; }
};

}
