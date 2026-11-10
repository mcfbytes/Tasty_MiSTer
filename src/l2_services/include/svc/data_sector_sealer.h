// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/seat.h"
#include "svc/track.h"

namespace mister::svc {

class DataSectorSealer final {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr std::size_t kEdcBytes = 4;
    static constexpr std::size_t kModeOneEdcSpan = 2064;
    static constexpr std::size_t kModeOneTailFirst = kModeOneEdcSpan + kEdcBytes;

    void seal(std::span<std::byte, kRawFrameBytes> sector) const noexcept;

private:
    [[nodiscard]] static std::uint32_t checksum(std::span<const std::byte> payload) noexcept;
};

static_assert(
    DataSectorSealer::kModeOneTailFirst <= kRawFrameBytes,
    "the zeroed tail lies inside the sector it seals"
);

}
