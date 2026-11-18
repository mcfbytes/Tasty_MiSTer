// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/decimal_text.h"

namespace mister::infra {

struct LwOffsetLabel {
    std::uint32_t v = 0;

    [[nodiscard]] DecimalText text() const noexcept { return DecimalText{v}; }

    friend constexpr bool operator==(LwOffsetLabel, LwOffsetLabel) = default;
};

static_assert(
    sizeof(LwOffsetLabel) == 4 && std::is_trivially_copyable_v<LwOffsetLabel>,
    "a log record field stays one word"
);

}
