// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/conf_switches.h"
#include "proto/status_bit.h"
#include "proto/status_word.h"

namespace mister::proto {

struct ResetTerms {
    StatusWord held{};
    StatusWord raised{};
    StatusWord changed{};
    ConfSwitches buttons{};

    [[nodiscard]] static constexpr ResetTerms framework() noexcept {
        ResetTerms t{};
        t.held = t.held.with(kResetBit, true);
        t.buttons.set(ConfSwitches::Bit::UserButton);
        return t;
    }

    [[nodiscard]] constexpr ResetTerms with_held(StatusBit bit) const noexcept {
        ResetTerms t = *this;
        t.held = t.held.with(bit, true);
        return t;
    }

    [[nodiscard]] constexpr ResetTerms with_raised(StatusBit bit) const noexcept {
        ResetTerms t = *this;
        t.raised = t.raised.with(bit, true);
        return t;
    }

    [[nodiscard]] constexpr ResetTerms with_changed(StatusBit bit) const noexcept {
        ResetTerms t = *this;
        t.changed = t.changed.with(bit, true);
        return t;
    }

    [[nodiscard]] constexpr bool status_trips(
        const StatusWord& sent,
        const StatusWord& next
    ) const noexcept {
        for (unsigned i = 0; i < StatusWord::kWords; ++i) {
            const hal::SpiWord was = sent.words[i], now = next.words[i];
            if (was.with(now).intersects(held.words[i]))
                return true;
            if (now.without(was).intersects(raised.words[i]))
                return true;
            if (now.without(was).with(was.without(now)).intersects(changed.words[i]))
                return true;
        }
        return false;
    }

    [[nodiscard]] constexpr bool buttons_trip(ConfSwitches sent, ConfSwitches next) const noexcept {
        return sent.intersects(buttons) || next.intersects(buttons);
    }
};

}
