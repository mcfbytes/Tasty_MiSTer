// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::cores::atari8 {

struct CartKib {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr CartKib floor_of(proto::FileSize size) noexcept {
        return CartKib{static_cast<std::uint16_t>(size / kKib)};
    }

    [[nodiscard]] constexpr proto::FileSize bytes() const noexcept {
        return proto::FileSize{std::uint64_t{v} * kKib};
    }

    friend constexpr bool operator==(CartKib, CartKib) = default;
    friend constexpr auto operator<=>(CartKib, CartKib) = default;

    static constexpr proto::FileSize kKib{1024};
};

}
