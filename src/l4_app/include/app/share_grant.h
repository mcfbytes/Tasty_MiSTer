// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "cores/registry.h"
#include "hal/types.h"
#include "infra/park_timeout.h"
#include "infra/path_text.h"
#include "infra/seat.h"
#include "infra/serial.h"
#include "infra/xthread/telemetry.h"
#include "proto/types.h"

namespace mister::app {

struct ShareSerial {
    std::uint32_t v = 0;
    using serial_rule = infra::serial::SkipZero<std::uint32_t>;

    [[nodiscard]] constexpr ShareSerial next() const noexcept { return infra::serial::next(*this); }

    friend constexpr bool operator==(ShareSerial, ShareSerial) = default;
};

struct ShareGrant {

    struct Open {
        cores::MakeShareHost make = nullptr;
        hal::FabricAddr page{};
        proto::FileSize bytes{};
        infra::ParkTimeout poll{infra::kForever};
        infra::PathText root{};
        ShareSerial open{};
        ShareSerial reset{};
    };

    std::optional<Open> want{};
};

using ShareGrantCell = xthread::Telemetry<ShareGrant, SeatTag::Main>;

}
