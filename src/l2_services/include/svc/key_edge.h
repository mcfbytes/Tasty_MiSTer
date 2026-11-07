// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/seat.h"
#include "svc/types.h"

namespace mister::svc {

struct KeyEdge {
    EvdevCode code{};
    bool pressed = false;
    friend constexpr bool operator==(KeyEdge, KeyEdge) = default;
};

inline constexpr std::size_t kMaxKeyEdges = 32;

class KeyEdgeBatch {
    TASTY_SEAT_EXEMPT(component);

public:
    constexpr void push(KeyEdge e) noexcept {
        if (count_ < kMaxKeyEdges) {
            edges_[count_++] = e;
        } else {
            ++dropped_;
        }
    }

    [[nodiscard]] constexpr std::size_t count() const noexcept { return count_; }

    [[nodiscard]] constexpr KeyEdge at(std::size_t i) const noexcept { return edges_[i]; }

    [[nodiscard]] constexpr std::uint16_t dropped() const noexcept { return dropped_; }

    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

    friend constexpr bool operator==(const KeyEdgeBatch&, const KeyEdgeBatch&) = default;

private:
    std::array<KeyEdge, kMaxKeyEdges> edges_{};
    std::size_t count_ = 0;
    std::uint16_t dropped_ = 0;
};

static_assert(
    sizeof(KeyEdgeBatch) < 256,
    "the batch is a fixed-capacity value on the T-INPUT drain path"
);

}
