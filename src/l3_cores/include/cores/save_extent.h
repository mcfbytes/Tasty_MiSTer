// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

enum class SaveAnswer : std::uint8_t { Pending, Known, Declined };

struct SaveExtent {
    std::uint32_t generation = 0;
    std::uint64_t size_bytes = 0;
    SaveAnswer answer = SaveAnswer::Pending;
};

enum class SaveVerdict : std::uint8_t { Known, Declined };

constexpr void set_verdict(SaveExtent& level, SaveVerdict v) noexcept {
    level.answer = v == SaveVerdict::Known ? SaveAnswer::Known : SaveAnswer::Declined;
}

}  // namespace mister::cores
