// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "proto/types.h"

namespace mister::cores::minimig {

struct ShareSeekTo {
    std::int32_t v = 0;

    [[nodiscard]] constexpr std::optional<proto::FileOffset> past(
        proto::FileOffset base
    ) const noexcept {
        return base.seek_by(v);
    }

    friend constexpr bool operator==(ShareSeekTo, ShareSeekTo) = default;
};

}
