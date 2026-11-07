// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

enum class SaveAnswer : std::uint8_t { Pending, Known, Declined };

struct SaveExtent {
    TASTY_SEAT_EXEMPT(const_shared);
    proto::ActKey generation{};
    proto::FileSize size_bytes{};
    SaveAnswer answer = SaveAnswer::Pending;
    bool exists = false;
};

enum class SaveVerdict : std::uint8_t { Known, Declined };

constexpr void set_verdict(SaveExtent& level, SaveVerdict v) noexcept {
    level.answer = v == SaveVerdict::Known ? SaveAnswer::Known : SaveAnswer::Declined;
}

}
