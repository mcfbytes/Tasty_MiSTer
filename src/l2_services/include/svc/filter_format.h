// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

enum class FilterEncoding : std::uint8_t { None = 0, Bytes = 1, Words = 2, WordsAdaptive = 3 };

struct FilterFormat {
    FilterEncoding encoding = FilterEncoding::None;
    bool full_precision = false;

    friend constexpr bool operator==(FilterFormat, FilterFormat) = default;
};

}

#include "svc/types.h"

namespace mister::svc {

constexpr FilterFormat FltFlags::format() const noexcept {
    return {encoding(), full_precision()};
}

}
