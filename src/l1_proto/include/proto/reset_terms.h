// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::proto {

struct ResetTerms {
    TASTY_SEAT_EXEMPT(component);
    StatusWord held{};
    StatusWord raised{};
    std::uint16_t buttons = 0;

    [[nodiscard]] static constexpr ResetTerms framework() noexcept {
        ResetTerms t{};
        t.held.words[0] = 1u;
        t.buttons = 1u << 1;
        return t;
    }
    [[nodiscard]] constexpr ResetTerms with_raised(StatusBit bit) const noexcept {
        ResetTerms t = *this;
        if (bit.v < StatusWord::kBits) {
            t.raised.words[bit.v / 16u] |= static_cast<std::uint16_t>(1u << (bit.v % 16u));
        }
        return t;
    }

    [[nodiscard]] constexpr bool status_trips(const StatusWord& sent,
                                              const StatusWord& next) const noexcept {
        for (unsigned i = 0; i < StatusWord::kWords; ++i) {
            const unsigned was = sent.words[i], now = next.words[i];
            if (((was | now) & held.words[i]) != 0u) return true;
            if ((~was & now & raised.words[i]) != 0u) return true;
        }
        return false;
    }
    [[nodiscard]] constexpr bool buttons_trip(std::uint16_t sent,
                                              std::uint16_t next) const noexcept {
        return ((static_cast<unsigned>(sent) | next) & buttons) != 0u;
    }
};

}  // namespace mister::proto
