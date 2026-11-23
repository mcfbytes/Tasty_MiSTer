// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/types.h"
#include "proto/types.h"
#include "proto/user_io/condition.h"

namespace mister::proto {

struct DiskStatusTag {
    ReplyMask mask{};
    ReplyWord expected{};

    [[nodiscard]] static constexpr DiskStatusTag of(ReplyWord status, ReplyMask mask) noexcept {
        return DiskStatusTag{mask, status.masked(mask)};
    }

    [[nodiscard]] constexpr bool holds(hal::SpiWord beat) const noexcept {
        return !user_io::condition_holds(
            user_io::Condition::MaskedChangedSinceLast,
            mask,
            ReplyWord::of(beat),
            expected
        );
    }

    friend constexpr bool operator==(DiskStatusTag, DiskStatusTag) = default;
};

}
