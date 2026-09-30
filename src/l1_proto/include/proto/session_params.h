// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace mister::proto {

enum class TransferDirection : std::uint8_t { Download = 0, Upload = 1 };

struct AuxValue {
    std::uint32_t v = 0;
    friend constexpr bool operator==(AuxValue, AuxValue) = default;
};

struct SessionParams {
    TransferDirection direction = TransferDirection::Download;
    AuxValue aux{};
    std::string_view ext{};
};

}  // namespace mister::proto
