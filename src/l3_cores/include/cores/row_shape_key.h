// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"

namespace mister::cores {

struct RowShapeKey {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr Error refused(Errc code, infra::ErrorSite site) const noexcept {
        return Error{code, site, v};
    }

    friend constexpr bool operator==(RowShapeKey, RowShapeKey) = default;
};

}
