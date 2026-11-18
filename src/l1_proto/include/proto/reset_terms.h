// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/status_bit.h"
#include "proto/status_word.h"

namespace mister::proto {

struct ResetTerms {
    StatusWord held{};
    StatusWord raised{};
    std::uint16_t buttons = 0;

    [[nodiscard]] static constexpr ResetTerms framework() noexcept {
        ResetTerms t{};
        t.held = t.held.with(kResetBit, true);
        t.buttons = 1u << 1;
        return t;
    }

    [[nodiscard]] constexpr ResetTerms with_raised(StatusBit bit) const noexcept {
        ResetTerms t = *this;
        t.raised = t.raised.with(bit, true);
        return t;
    }

    [[nodiscard]] constexpr bool status_trips(
        const StatusWord& sent,
        const StatusWord& next
    ) const noexcept {
        for (unsigned i = 0; i < StatusWord::kWords; ++i) {
            const hal::SpiWord was = sent.words[i], now = next.words[i];
            if (was.with(now).any_of(held.words[i]))
                return true;
            if (now.without(was).any_of(raised.words[i]))
                return true;
        }
        return false;
    }

    [[nodiscard]] constexpr bool buttons_trip(
        std::uint16_t sent,
        std::uint16_t next
    ) const noexcept {
        return ((static_cast<unsigned>(sent) | next) & buttons) != 0u;
    }
};

}
