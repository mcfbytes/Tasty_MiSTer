// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "cores/minimig/a2065_mac.h"
#include "infra/seat.h"
#include "svc/nic_mac.h"

namespace mister::cores::minimig {

class A2065MacMunge {
    TASTY_SEAT_RESIDENT(Nic);

public:
    void set(const A2065Mac& mac) noexcept {
        station_ = mac.station;
        wire_ = mac.wire;
    }

    void set_station(svc::MacAddress station) noexcept { station_ = station; }

    [[nodiscard]] const svc::MacAddress& wire() const noexcept { return wire_; }

    [[nodiscard]] const svc::MacAddress& station() const noexcept { return station_; }

    void munge(std::span<std::byte> frame) const noexcept;

private:
    bool swap_(std::span<std::byte> at) const noexcept;

    svc::MacAddress station_{};
    svc::MacAddress wire_{};
};

}
