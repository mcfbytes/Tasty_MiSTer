// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores::ide {

struct HotSwapBit {
    TASTY_SEAT_EXEMPT(component);
    proto::ReplyMask bit{};
    enum class AllowWhen : std::uint8_t { Set, Clear };
    AllowWhen allow = AllowWhen::Set;

    [[nodiscard]] constexpr bool allows(proto::ReplyWord head) const noexcept {
        return head.intersects(bit) == (allow == AllowWhen::Set);
    }

    friend constexpr bool operator==(HotSwapBit, HotSwapBit) = default;
};

}
