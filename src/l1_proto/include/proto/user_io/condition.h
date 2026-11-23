// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "proto/types.h"

namespace mister::proto::user_io {

enum class Condition : std::uint8_t { NonZero, MaskedChangedSinceLast, Zero, kCount };

[[nodiscard]] constexpr bool condition_holds(
    Condition c,
    ReplyMask mask,
    ReplyWord word,
    std::optional<ReplyWord> last_seen
) noexcept {
    switch (c) {
        case Condition::NonZero:
            return word.intersects(mask);
        case Condition::MaskedChangedSinceLast:
            return !last_seen || word.masked(mask) != last_seen->masked(mask);
        case Condition::Zero:
            return !word.intersects(mask);
        case Condition::kCount:
            break;
    }
    return false;
}

static_assert(
    condition_holds(Condition::Zero, ReplyMask{0x8000}, ReplyWord{0x7FFF}, {})
    && !condition_holds(Condition::Zero, ReplyMask{0x8000}, ReplyWord{0x8000}, {})
);

}
