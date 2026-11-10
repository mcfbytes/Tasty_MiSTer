// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <type_traits>

#include "proto/bit_field.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::proto {

struct ItemProbe {
    BitField bits{};
    FieldValue active{};

    [[nodiscard]] static constexpr std::optional<ItemProbe> of(
        BitField bits,
        FieldValue active
    ) noexcept {
        if (!bits.valid())
            return std::nullopt;
        if (unsigned{bits.start.v} + bits.width > StatusWord::kBits)
            return std::nullopt;
        return ItemProbe{bits, active};
    }

    friend constexpr bool operator==(ItemProbe, ItemProbe) = default;
};

static_assert(std::is_trivially_copyable_v<ItemProbe>);

}
