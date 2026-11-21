// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

enum class SaveVerdict : std::uint8_t { Known, Declined };

struct SaveExtent {
    TASTY_SEAT_EXEMPT(const_shared);
    proto::ActKey act{};
    proto::FileSize size_bytes{};

    std::optional<SaveVerdict> answer{};
    bool exists = false;
};

constexpr void set_verdict(SaveExtent& level, SaveVerdict v) noexcept {
    level.answer = v;
}

}
