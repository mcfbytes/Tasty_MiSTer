// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

#include "cores/config_codec.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "proto/types.h"

namespace mister::app {

struct ConfigValueImage {
    TASTY_SEAT_EXEMPT(component);
    std::array<std::byte, cores::kMaxConfigValueBytes> bytes{};
    proto::FileSize size{};

    [[nodiscard]] std::span<const std::byte> view() const noexcept {
        return std::span<const std::byte>(bytes).first(
            size.clamp_write(proto::kFileStart, bytes.size())
        );
    }
};

static_assert(std::is_trivially_copyable_v<ConfigValueImage>);

using ConfigValueCell = xthread::Telemetry<ConfigValueImage, SeatTag::RT>;

}
