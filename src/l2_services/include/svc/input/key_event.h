// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/seat.h"
#include "svc/types.h"

namespace mister::svc::input {

struct KeyEvent {
    EvdevCode code{};
    bool pressed = false;
    friend constexpr bool operator==(KeyEvent, KeyEvent) = default;
};

inline constexpr std::size_t kMaxKeyEvents = 32;

class KeyEventBatch {
    TASTY_SEAT_EXEMPT(component);

public:
    constexpr void push(KeyEvent e) noexcept {
        if (count_ < kMaxKeyEvents) {
            edges_[count_++] = e;
        } else {
            ++dropped_;
        }
    }

    [[nodiscard]] constexpr std::size_t count() const noexcept { return count_; }

    [[nodiscard]] constexpr KeyEvent at(std::size_t i) const noexcept { return edges_[i]; }

    [[nodiscard]] constexpr std::uint16_t dropped() const noexcept { return dropped_; }

    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

    friend constexpr bool operator==(const KeyEventBatch&, const KeyEventBatch&) = default;

private:
    std::array<KeyEvent, kMaxKeyEvents> edges_{};
    std::size_t count_ = 0;
    std::uint16_t dropped_ = 0;
};

static_assert(
    sizeof(KeyEventBatch) < 256,
    "the batch is a fixed-capacity value on the T-INPUT drain path"
);

}
