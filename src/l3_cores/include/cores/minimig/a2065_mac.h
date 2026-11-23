// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "svc/nic_host_seed.h"
#include "svc/nic_mac.h"

namespace mister::cores::minimig {

struct A2065Mac {

    static constexpr svc::MacAddress::Oui kCommodore{
        std::byte{0x00},
        std::byte{0x80},
        std::byte{0x10}
    };

    svc::MacAddress station{};
    svc::MacAddress wire{};

    [[nodiscard]] static constexpr svc::MacAddress from(const svc::MacAddress& nic) noexcept {
        return nic.under(kCommodore);
    }

    [[nodiscard]] static constexpr svc::MacAddress from_seed(svc::NicHostSeed seed) noexcept {
        std::array<std::byte, sizeof(std::uint32_t)> b{};
        seed.store_be(b);
        svc::MacAddress m = svc::MacAddress::of_nic_half(
            std::span<const std::byte>(b)
                .last<svc::MacAddress::kBytes - svc::MacAddress::kOuiBytes>()
        );
        if (m.low_half_zero())
            m.bytes.back() = kNonZeroLow;
        return m;
    }

    [[nodiscard]] static constexpr A2065Mac resolve(
        const svc::MacAddress& opened,
        std::optional<svc::MacAddress> onboard,
        svc::NicHostSeed seed
    ) noexcept {
        const svc::MacAddress low = !opened.low_half_zero() ? opened
            : onboard                                       ? *onboard
                                                            : from_seed(seed);
        return A2065Mac{.station = from(low), .wire = from(low)};
    }

    friend constexpr bool operator==(const A2065Mac&, const A2065Mac&) = default;

private:
    static constexpr std::byte kNonZeroLow{0x01};
};

}
