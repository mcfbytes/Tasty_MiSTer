// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

#include "proto/types.h"

namespace mister::proto::user_io {

struct HeadLengths {

    using Table = std::array<ReplyCount, 16>;

    ReplyIndex head{};
    Table more{};

    [[nodiscard]] constexpr ReplyCount after(ReplyWord head_word) const noexcept {
        return more[head_word.nibble()];
    }

    [[nodiscard]] constexpr ReplyCount widest() const noexcept { return std::ranges::max(more); }

    friend constexpr bool operator==(const HeadLengths&, const HeadLengths&) = default;
};

}
