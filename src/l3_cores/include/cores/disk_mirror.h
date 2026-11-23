// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/core_window_decl.h"
#include "cores/window_spot.h"
#include "proto/types.h"

namespace mister::cores {

struct DiskMirror {
    WindowSpot at;
    proto::FileSize room;

    [[nodiscard]] constexpr bool fits(std::span<const CoreWindowDecl> windows) const noexcept {
        return !room.empty() && at.holds(windows, room);
    }

    [[nodiscard]] constexpr bool overlaps(
        const DiskMirror& other,
        std::span<const CoreWindowDecl> windows
    ) const noexcept {
        if (!fits(windows) || !other.fits(windows))
            return true;
        const auto mine = at.slot.in(windows);
        const auto theirs = other.at.slot.in(windows);
        if (!mine || !theirs)
            return true;
        const proto::FileSize my_at = at.offset.from_base(proto::FileSize{mine->region.offset});
        const proto::FileSize their_at = other.at.offset.from_base(
            proto::FileSize{theirs->region.offset}
        );
        return my_at < their_at + other.room && their_at < my_at + room;
    }

    friend constexpr bool operator==(const DiskMirror&, const DiskMirror&) = default;
};

}
