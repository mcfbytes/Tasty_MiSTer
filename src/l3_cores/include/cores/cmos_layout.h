// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

struct CmosLayout {
    TASTY_SEAT_EXEMPT(const_shared);

    static constexpr std::size_t kClockBytes = 16;
    static constexpr infra::ByteCount kClock{kClockBytes};

    infra::ByteCount bytes{};
    infra::ByteOffset year_at{};
    infra::ByteOffset century_at{};
    std::byte century{};

    proto::WideIoIndex index{};

    [[nodiscard]] constexpr bool outside_clock(infra::ByteOffset at) const noexcept {
        return at >= infra::affine::end_of<infra::ByteOffset>(kClock)
            && infra::affine::in_bounds(bytes, at);
    }

    [[nodiscard]] constexpr bool well_formed() const noexcept {
        return outside_clock(year_at) && outside_clock(century_at) && year_at != century_at;
    }
};

}
