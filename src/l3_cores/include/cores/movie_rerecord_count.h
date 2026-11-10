// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/decimal_text.h"

namespace mister::cores {

struct MovieRerecordCount {
    std::uint32_t v = 0;

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(MovieRerecordCount, MovieRerecordCount) = default;
};

}
